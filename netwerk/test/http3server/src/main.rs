// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use base64::prelude::*;
use neqo_bin::server::{HttpServer, Runner};
use neqo_common::{event::Provider, qdebug, qtrace, Datagram, Header};
use neqo_crypto::{generate_ech_keys, init_db, AllowZeroRtt, AntiReplay};
use neqo_http3::{
    Error, Http3OrWebTransportStream, Http3Parameters, Http3Server, Http3ServerEvent,
    WebTransportRequest, WebTransportServerEvent, WebTransportSessionAcceptAction,
};
use neqo_transport::server::ConnectionRef;
use neqo_transport::{
    ConnectionEvent, ConnectionParameters, OutputBatch, RandomConnectionIdGenerator, StreamType,
};
use std::env;

use std::cell::RefCell;
use std::io;
use std::path::PathBuf;
use std::process::exit;
use std::rc::Rc;
use std::thread;
use std::time::{Duration, Instant};

use cfg_if::cfg_if;

cfg_if! {
    if #[cfg(not(target_os = "android"))] {
        use std::sync::mpsc::{channel, Receiver, TryRecvError};
        use hyper::body::HttpBody;
        use hyper::header::{HeaderName, HeaderValue};
        use hyper::{Body, Client, Method, Request};
    }
}

use std::cmp::min;
use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::collections::HashSet;
use std::hash::{Hash, Hasher};
use std::net::SocketAddr;

const MAX_TABLE_SIZE: u64 = 65536;
const MAX_BLOCKED_STREAMS: u16 = 10;
const PROTOCOLS: &[&str] = &["h3"];
const ECH_CONFIG_ID: u8 = 7;
const ECH_PUBLIC_NAME: &str = "public.example";

const HTTP_RESPONSE_WITH_WRONG_FRAME: &[u8] = &[
    0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x37, // headers
    0x0, 0x3, 0x61, 0x62, 0x63, // the first data frame
    0x3, 0x1, 0x5, // a cancel push frame that is not allowed
];
struct Http3TestServer {
    server: Http3Server,
    // This a map from a post request to amount of data ithas been received on the request.
    // The respons will carry the amount of data received.
    posts: HashMap<Http3OrWebTransportStream, usize>,
    responses: HashMap<Http3OrWebTransportStream, Vec<u8>>,
    connections_to_close: HashMap<Instant, Vec<ConnectionRef>>,
    current_connection_hash: u64,
    sessions_to_close: HashMap<Instant, Vec<WebTransportRequest>>,
    sessions_to_create_stream: Vec<(WebTransportRequest, StreamType, Option<Vec<u8>>)>,
    webtransport_bidi_stream: HashSet<Http3OrWebTransportStream>,
    wt_unidi_conn_to_stream: HashMap<ConnectionRef, Http3OrWebTransportStream>,
    wt_unidi_echo_back: HashMap<Http3OrWebTransportStream, Http3OrWebTransportStream>,
    received_datagram: Option<Vec<u8>>,
}

impl ::std::fmt::Display for Http3TestServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "{}", self.server)
    }
}
impl Http3TestServer {
    pub fn new(server: Http3Server) -> Self {
        Self {
            server,
            posts: HashMap::new(),
            responses: HashMap::new(),
            connections_to_close: HashMap::new(),
            current_connection_hash: 0,
            sessions_to_close: HashMap::new(),
            sessions_to_create_stream: Vec::new(),
            webtransport_bidi_stream: HashSet::new(),
            wt_unidi_conn_to_stream: HashMap::new(),
            wt_unidi_echo_back: HashMap::new(),
            received_datagram: None,
        }
    }

    fn new_response(&mut self, stream: Http3OrWebTransportStream, mut data: Vec<u8>) {
        if data.len() == 0 {
            let _ = stream.stream_close_send();
            return;
        }
        match stream.send_data(&data) {
            Ok(sent) => {
                if sent < data.len() {
                    self.responses.insert(stream, data.split_off(sent));
                } else {
                    stream.stream_close_send().unwrap();
                }
            }
            Err(e) => {
                eprintln!("error is {:?}", e);
            }
        }
    }

    fn handle_stream_writable(&mut self, stream: Http3OrWebTransportStream) {
        if let Some(data) = self.responses.get_mut(&stream) {
            match stream.send_data(&data) {
                Ok(sent) => {
                    if sent < data.len() {
                        let new_d = (*data).split_off(sent);
                        *data = new_d;
                    } else {
                        stream.stream_close_send().unwrap();
                        self.responses.remove(&stream);
                    }
                }
                Err(_) => {
                    eprintln!("Unexpected error");
                }
            }
        }
    }

    fn maybe_close_session(&mut self) {
        let now = Instant::now();
        for (expires, sessions) in self.sessions_to_close.iter_mut() {
            if *expires <= now {
                for s in sessions.iter_mut() {
                    drop(s.close_session(0, ""));
                }
            }
        }
        self.sessions_to_close.retain(|expires, _| *expires >= now);
    }

    fn maybe_close_connection(&mut self) {
        let now = Instant::now();
        for (expires, connections) in self.connections_to_close.iter_mut() {
            if *expires <= now {
                for c in connections.iter_mut() {
                    c.borrow_mut().close(now, 0x0100, "");
                }
            }
        }
        self.connections_to_close
            .retain(|expires, _| *expires >= now);
    }

    fn maybe_create_wt_stream(&mut self) {
        if self.sessions_to_create_stream.is_empty() {
            return;
        }
        let tuple = self.sessions_to_create_stream.pop().unwrap();
        let session = tuple.0;
        let wt_server_stream = session.create_stream(tuple.1).unwrap();
        if tuple.1 == StreamType::UniDi {
            if let Some(data) = tuple.2 {
                self.new_response(wt_server_stream, data);
            } else {
                // relaying Http3ServerEvent::Data to uni streams
                // slows down netwerk/test/unit/test_webtransport_simple.js
                // to the point of failure. Only do so when necessary.
                self.wt_unidi_conn_to_stream
                    .insert(wt_server_stream.conn.clone(), wt_server_stream);
            }
        } else {
            if let Some(data) = tuple.2 {
                self.new_response(wt_server_stream, data);
            } else {
                self.webtransport_bidi_stream.insert(wt_server_stream);
            }
        }
    }
}

impl HttpServer for Http3TestServer {
    fn process_multiple(
        &mut self,
        dgram: Option<Datagram<&mut [u8]>>,
        now: Instant,
        max_datagrams: std::num::NonZeroUsize,
    ) -> OutputBatch {
        let output = self.server.process_multiple(dgram, now, max_datagrams);

        let output = if self.sessions_to_close.is_empty() && self.connections_to_close.is_empty() {
            output
        } else {
            // In case there are pending sessions to close, use a shorter
            // timeout to make process_events() to be called earlier.
            const MIN_INTERVAL: Duration = Duration::from_millis(100);

            match output {
                OutputBatch::None => OutputBatch::Callback(MIN_INTERVAL),
                o @ OutputBatch::DatagramBatch(_) => o,
                OutputBatch::Callback(d) => OutputBatch::Callback(min(d, MIN_INTERVAL)),
            }
        };

        output
    }

    fn process_events(&mut self, now: Instant) {
        self.maybe_close_connection();
        self.maybe_close_session();
        self.maybe_create_wt_stream();

        while let Some(event) = self.server.next_event() {
            qtrace!("Event: {:?}", event);
            match event {
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin,
                } => {
                    qtrace!("Headers (request={} fin={}): {:?}", stream, fin, headers);

                    // Some responses do not have content-type. This is on purpose to exercise
                    // UnknownDecoder code.
                    let default_ret = b"Hello World".to_vec();
                    let default_headers = vec![
                        Header::new(":status", "200"),
                        Header::new("cache-control", "no-cache"),
                        Header::new("content-length", default_ret.len().to_string()),
                        Header::new(
                            "x-http3-conn-hash",
                            self.current_connection_hash.to_string(),
                        ),
                    ];

                    let path_hdr = headers.iter().find(|&h| h.name() == ":path");
                    match path_hdr {
                        Some(ph) if !ph.value().is_empty() => {
                            let path = ph.value();
                            qtrace!("Serve request {}", path);
                            if path == "/Response421" {
                                let response_body = b"0123456789".to_vec();
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "421"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new(
                                            "content-length",
                                            response_body.len().to_string(),
                                        ),
                                    ])
                                    .unwrap();
                                self.new_response(stream, response_body);
                            } else if path == "/RequestCancelled" {
                                stream
                                    .stream_stop_sending(Error::HttpRequestCancelled.code())
                                    .unwrap();
                                stream
                                    .stream_reset_send(Error::HttpRequestCancelled.code())
                                    .unwrap();
                            } else if path == "/VersionFallback" {
                                stream
                                    .stream_stop_sending(Error::HttpVersionFallback.code())
                                    .unwrap();
                                stream
                                    .stream_reset_send(Error::HttpVersionFallback.code())
                                    .unwrap();
                            } else if path == "/EarlyResponse" {
                                stream.stream_stop_sending(Error::HttpNone.code()).unwrap();
                            } else if path == "/RequestRejected" {
                                stream
                                    .stream_stop_sending(Error::HttpRequestRejected.code())
                                    .unwrap();
                                stream
                                    .stream_reset_send(Error::HttpRequestRejected.code())
                                    .unwrap();
                            } else if path == "/closeafter1000ms" {
                                let response_body = b"0123456789".to_vec();
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new(
                                            "content-length",
                                            response_body.len().to_string(),
                                        ),
                                    ])
                                    .unwrap();
                                let expires = Instant::now() + Duration::from_millis(1000);
                                if !self.connections_to_close.contains_key(&expires) {
                                    self.connections_to_close.insert(expires, Vec::new());
                                }
                                self.connections_to_close
                                    .get_mut(&expires)
                                    .unwrap()
                                    .push(stream.conn.clone());

                                self.new_response(stream, response_body);
                            } else if path == "/.well-known/http-opportunistic" {
                                let host_hdr = headers.iter().find(|&h| h.name() == ":authority");
                                match host_hdr {
                                    Some(host) if !host.value().is_empty() => {
                                        let mut content = b"[\"http://".to_vec();
                                        content.extend(host.value().as_bytes());
                                        content.extend(b"\"]".to_vec());
                                        stream
                                            .send_headers(&[
                                                Header::new(":status", "200"),
                                                Header::new("cache-control", "no-cache"),
                                                Header::new("content-type", "application/json"),
                                                Header::new(
                                                    "content-length",
                                                    content.len().to_string(),
                                                ),
                                            ])
                                            .unwrap();
                                        self.new_response(stream, content);
                                    }
                                    _ => {
                                        stream.send_headers(&default_headers).unwrap();
                                        self.new_response(stream, default_ret);
                                    }
                                }
                            } else if path == "/no_body" {
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                    ])
                                    .unwrap();
                                stream.stream_close_send().unwrap();
                            } else if path == "/no_content_length" {
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                    ])
                                    .unwrap();
                                self.new_response(stream, vec![b'a'; 4000]);
                            } else if path == "/content_length_smaller" {
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-type", "text/plain"),
                                        Header::new("content-length", 4000.to_string()),
                                    ])
                                    .unwrap();
                                self.new_response(stream, vec![b'a'; 8000]);
                            } else if path == "/post" {
                                // Read all data before responding.
                                self.posts.insert(stream, 0);
                            } else if path == "/priority_mirror" {
                                if let Some(priority) =
                                    headers.iter().find(|h| h.name() == "priority")
                                {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                            Header::new("content-type", "text/plain"),
                                            Header::new("priority-mirror", priority.value()),
                                            Header::new(
                                                "content-length",
                                                priority.value().len().to_string(),
                                            ),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, priority.value().as_bytes().to_vec());
                                } else {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                        ])
                                        .unwrap();
                                    stream.stream_close_send().unwrap();
                                }
                            } else if path == "/103_response" {
                                if let Some(early_hint) =
                                    headers.iter().find(|h| h.name() == "link-to-set")
                                {
                                    for l in early_hint.value().split(',') {
                                        stream
                                            .send_headers(&[
                                                Header::new(":status", "103"),
                                                Header::new("link", l),
                                            ])
                                            .unwrap();
                                    }
                                }
                                stream
                                    .send_headers(&[
                                        Header::new(":status", "200"),
                                        Header::new("cache-control", "no-cache"),
                                        Header::new("content-length", "0"),
                                    ])
                                    .unwrap();
                                stream.stream_close_send().unwrap();
                            } else if path == "/get_webtransport_datagram" {
                                if let Some(vec_ref) = self.received_datagram.as_ref() {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new(
                                                "content-length",
                                                vec_ref.len().to_string(),
                                            ),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, vec_ref.to_vec());
                                    self.received_datagram = None;
                                } else {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "404"),
                                            Header::new("cache-control", "no-cache"),
                                        ])
                                        .unwrap();
                                    stream.stream_close_send().unwrap();
                                }
                            } else if path == "/alt_svc_header" {
                                if let Some(alt_svc) =
                                    headers.iter().find(|h| h.name() == "x-altsvc")
                                {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                            Header::new("content-type", "text/plain"),
                                            Header::new("content-length", 100.to_string()),
                                            Header::new(
                                                "alt-svc",
                                                format!("h3={}", alt_svc.value()),
                                            ),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, vec![b'a'; 100]);
                                } else {
                                    stream
                                        .send_headers(&[
                                            Header::new(":status", "200"),
                                            Header::new("cache-control", "no-cache"),
                                        ])
                                        .unwrap();
                                    self.new_response(stream, vec![b'a'; 100]);
                                }
                            } else {
                                match path.trim_matches(|p| p == '/').parse::<usize>() {
                                    Ok(v) => {
                                        stream
                                            .send_headers(&[
                                                Header::new(":status", "200"),
                                                Header::new("cache-control", "no-cache"),
                                                Header::new("content-type", "text/plain"),
                                                Header::new("content-length", v.to_string()),
                                            ])
                                            .unwrap();
                                        self.new_response(stream, vec![b'a'; v]);
                                    }
                                    Err(_) => {
                                        stream.send_headers(&default_headers).unwrap();
                                        self.new_response(stream, default_ret);
                                    }
                                }
                            }
                        }
                        _ => {
                            stream.send_headers(&default_headers).unwrap();
                            self.new_response(stream, default_ret);
                        }
                    }
                }
                Http3ServerEvent::Data { stream, data, fin } => {
                    // echo bidirectional input back to client
                    if self.webtransport_bidi_stream.contains(&stream) {
                        if stream.handler.borrow().state().active() {
                            self.new_response(stream, data);
                        }
                        break;
                    }

                    // echo unidirectional input to back to client
                    // need to close or we hang
                    if self.wt_unidi_echo_back.contains_key(&stream) {
                        let echo_back = self.wt_unidi_echo_back.remove(&stream).unwrap();
                        echo_back.send_data(&data).unwrap();
                        echo_back.stream_close_send().unwrap();
                        break;
                    }

                    if let Some(r) = self.posts.get_mut(&stream) {
                        *r += data.len();
                    }
                    if fin {
                        if let Some(r) = self.posts.remove(&stream) {
                            let default_ret = b"Hello World".to_vec();
                            stream
                                .send_headers(&[
                                    Header::new(":status", "200"),
                                    Header::new("cache-control", "no-cache"),
                                    Header::new("x-data-received-length", r.to_string()),
                                    Header::new("content-length", default_ret.len().to_string()),
                                ])
                                .unwrap();
                            self.new_response(stream, default_ret);
                        }
                    }
                }
                Http3ServerEvent::DataWritable { stream } => self.handle_stream_writable(stream),
                Http3ServerEvent::StateChange { conn, state } => {
                    if matches!(state, neqo_http3::Http3State::Connected) {
                        let mut h = DefaultHasher::new();
                        conn.hash(&mut h);
                        self.current_connection_hash = h.finish();
                    }
                }
                Http3ServerEvent::PriorityUpdate { .. } => {}
                Http3ServerEvent::StreamReset { stream, error } => {
                    qtrace!("Http3ServerEvent::StreamReset {:?} {:?}", stream, error);
                }
                Http3ServerEvent::StreamStopSending { stream, error } => {
                    qtrace!(
                        "Http3ServerEvent::StreamStopSending {:?} {:?}",
                        stream,
                        error
                    );
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::NewSession {
                    session,
                    headers,
                }) => {
                    qdebug!(
                        "WebTransportServerEvent::NewSession {:?} {:?}",
                        session,
                        headers
                    );
                    let path_hdr = headers.iter().find(|&h| h.name() == ":path");
                    match path_hdr {
                        Some(ph) if !ph.value().is_empty() => {
                            let path = ph.value();
                            qtrace!("Serve request {}", path);
                            if path == "/success" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                            } else if path == "/redirect" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Reject(
                                        [
                                            Header::new(":status", "302"),
                                            Header::new("location", "/"),
                                        ]
                                        .to_vec(),
                                    ))
                                    .unwrap();
                            } else if path == "/reject" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Reject(
                                        [Header::new(":status", "404")].to_vec(),
                                    ))
                                    .unwrap();
                            } else if path == "/closeafter0ms" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                if !self.sessions_to_close.contains_key(&now) {
                                    self.sessions_to_close.insert(now, Vec::new());
                                }
                                self.sessions_to_close.get_mut(&now).unwrap().push(session);
                            } else if path == "/closeafter100ms" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                let expires = Instant::now() + Duration::from_millis(100);
                                if !self.sessions_to_close.contains_key(&expires) {
                                    self.sessions_to_close.insert(expires, Vec::new());
                                }
                                self.sessions_to_close
                                    .get_mut(&expires)
                                    .unwrap()
                                    .push(session);
                            } else if path == "/create_unidi_stream" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::UniDi,
                                    None,
                                ));
                            } else if path == "/create_unidi_stream_and_hello" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::UniDi,
                                    Some(Vec::from("qwerty")),
                                ));
                            } else if path == "/create_bidi_stream" {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::BiDi,
                                    None,
                                ));
                            } else if path == "/create_bidi_stream_and_hello" {
                                self.webtransport_bidi_stream.clear();
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::BiDi,
                                    Some(Vec::from("asdfg")),
                                ));
                            } else if path == "/create_bidi_stream_and_large_data" {
                                self.webtransport_bidi_stream.clear();
                                let data: Vec<u8> = vec![1u8; 32 * 1024 * 1024];
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                                self.sessions_to_create_stream.push((
                                    session,
                                    StreamType::BiDi,
                                    Some(data),
                                ));
                            } else {
                                session
                                    .response(&WebTransportSessionAcceptAction::Accept)
                                    .unwrap();
                            }
                        }
                        _ => {
                            session
                                .response(&WebTransportSessionAcceptAction::Reject(
                                    [Header::new(":status", "404")].to_vec(),
                                ))
                                .unwrap();
                        }
                    }
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::SessionClosed {
                    session,
                    reason,
                    headers: _,
                }) => {
                    qdebug!(
                        "WebTransportServerEvent::SessionClosed {:?} {:?}",
                        session,
                        reason
                    );
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::NewStream(stream)) => {
                    // new stream could be from client-outgoing unidirectional
                    // or bidirectional
                    if !stream.stream_info.is_http() {
                        if stream.stream_id().is_bidi() {
                            self.webtransport_bidi_stream.insert(stream);
                        } else {
                            // Newly created stream happens on same connection
                            // as the stream creation for client's incoming stream.
                            // Link the streams with map for echo back
                            if self.wt_unidi_conn_to_stream.contains_key(&stream.conn) {
                                let s = self.wt_unidi_conn_to_stream.remove(&stream.conn).unwrap();
                                self.wt_unidi_echo_back.insert(stream, s);
                            }
                        }
                    }
                }
                Http3ServerEvent::WebTransport(WebTransportServerEvent::Datagram {
                    session,
                    datagram,
                }) => {
                    qdebug!(
                        "WebTransportServerEvent::Datagram {:?} {:?}",
                        session,
                        datagram
                    );
                    self.received_datagram = Some(datagram);
                }
            }
        }
    }

    fn has_events(&self) -> bool {
        self.server.has_events()
    }
}

struct Server(neqo_transport::server::Server);

impl ::std::fmt::Display for Server {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        self.0.fmt(f)
    }
}

impl HttpServer for Server {
    fn process_multiple(
        &mut self,
        dgram: Option<Datagram<&mut [u8]>>,
        now: Instant,
        max_datagrams: std::num::NonZeroUsize,
    ) -> OutputBatch {
        self.0.process_multiple(dgram, now, max_datagrams)
    }

    fn process_events(&mut self, _now: Instant) {
        let active_conns = self.0.active_connections();
        for acr in active_conns {
            loop {
                let event = match acr.borrow_mut().next_event() {
                    None => break,
                    Some(e) => e,
                };
                match event {
                    ConnectionEvent::RecvStreamReadable { stream_id } => {
                        if stream_id.is_bidi() && stream_id.is_client_initiated() {
                            // We are only interesting in request streams
                            acr.borrow_mut()
                                .stream_send(stream_id, HTTP_RESPONSE_WITH_WRONG_FRAME)
                                .expect("Read should succeed");
                        }
                    }
                    _ => {}
                }
            }
        }
    }

    fn has_events(&self) -> bool {
        self.0.has_active_connections()
    }
}

struct Http3ProxyServer {
    server: Http3Server,
    responses: HashMap<Http3OrWebTransportStream, Vec<u8>>,
    server_port: i32,
    requests: HashMap<Http3OrWebTransportStream, (Vec<Header>, Vec<u8>)>,
    #[cfg(not(target_os = "android"))]
    response_to_send: HashMap<Http3OrWebTransportStream, Receiver<(Vec<Header>, Vec<u8>)>>,
}

impl ::std::fmt::Display for Http3ProxyServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "{}", self.server)
    }
}

impl Http3ProxyServer {
    pub fn new(server: Http3Server, server_port: i32) -> Self {
        Self {
            server,
            responses: HashMap::new(),
            server_port,
            requests: HashMap::new(),
            #[cfg(not(target_os = "android"))]
            response_to_send: HashMap::new(),
        }
    }

    #[cfg(not(target_os = "android"))]
    fn new_response(&mut self, stream: Http3OrWebTransportStream, mut data: Vec<u8>) {
        if data.len() == 0 {
            let _ = stream.stream_close_send();
            return;
        }
        match stream.send_data(&data) {
            Ok(sent) => {
                if sent < data.len() {
                    self.responses.insert(stream, data.split_off(sent));
                } else {
                    stream.stream_close_send().unwrap();
                }
            }
            Err(e) => {
                eprintln!("error is {:?}, stream will be reset", e);
                let _ = stream.stream_reset_send(Error::HttpRequestCancelled.code());
            }
        }
    }

    fn handle_stream_writable(&mut self, stream: Http3OrWebTransportStream) {
        if let Some(data) = self.responses.get_mut(&stream) {
            match stream.send_data(&data) {
                Ok(sent) => {
                    if sent < data.len() {
                        let new_d = (*data).split_off(sent);
                        *data = new_d;
                    } else {
                        stream.stream_close_send().unwrap();
                        self.responses.remove(&stream);
                    }
                }
                Err(_) => {
                    eprintln!("Unexpected error");
                }
            }
        }
    }

    #[cfg(not(target_os = "android"))]
    async fn fetch_url(
        request: Request<Body>,
        out_header: &mut Vec<Header>,
        out_body: &mut Vec<u8>,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let client = Client::new();
        let mut resp = client.request(request).await?;
        out_header.push(Header::new(":status", resp.status().as_str()));
        for (key, value) in resp.headers() {
            out_header.push(Header::new(
                key.as_str().to_ascii_lowercase(),
                match value.to_str() {
                    Ok(str) => str,
                    _ => "",
                },
            ));
        }

        while let Some(chunk) = resp.body_mut().data().await {
            match chunk {
                Ok(data) => {
                    out_body.append(&mut data.to_vec());
                }
                _ => {}
            }
        }

        Ok(())
    }

    #[cfg(not(target_os = "android"))]
    fn fetch(
        &mut self,
        stream: Http3OrWebTransportStream,
        request_headers: &Vec<Header>,
        request_body: Vec<u8>,
    ) {
        let mut request: Request<Body> = Request::default();
        let mut path = String::new();
        for hdr in request_headers.iter() {
            match hdr.name() {
                ":method" => {
                    *request.method_mut() = Method::from_bytes(hdr.value().as_bytes()).unwrap();
                }
                ":scheme" => {}
                ":authority" => {
                    request.headers_mut().insert(
                        hyper::header::HOST,
                        HeaderValue::from_str(hdr.value()).unwrap(),
                    );
                }
                ":path" => {
                    path = String::from(hdr.value());
                }
                _ => {
                    if let Ok(hdr_name) = HeaderName::from_lowercase(hdr.name().as_bytes()) {
                        request
                            .headers_mut()
                            .insert(hdr_name, HeaderValue::from_str(hdr.value()).unwrap());
                    }
                }
            }
        }
        *request.body_mut() = Body::from(request_body);
        *request.uri_mut() =
            match format!("http://127.0.0.1:{}{}", self.server_port.to_string(), path).parse() {
                Ok(uri) => uri,
                _ => {
                    eprintln!("invalid uri: {}", path);
                    stream
                        .send_headers(&[
                            Header::new(":status", "400"),
                            Header::new("cache-control", "no-cache"),
                            Header::new("content-length", "0"),
                        ])
                        .unwrap();
                    return;
                }
            };
        qtrace!("request header: {:?}", request);

        let (sender, receiver) = channel();
        thread::spawn(move || {
            let rt = tokio::runtime::Runtime::new().unwrap();
            let mut h: Vec<Header> = Vec::new();
            let mut data: Vec<u8> = Vec::new();
            let _ = rt.block_on(Self::fetch_url(request, &mut h, &mut data));
            qtrace!("response headers: {:?}", h);
            qtrace!("res data: {:02X?}", data);

            match sender.send((h, data)) {
                Ok(()) => {}
                _ => {
                    eprintln!("sender.send failed");
                }
            }
        });
        self.response_to_send.insert(stream, receiver);
    }

    #[cfg(target_os = "android")]
    fn fetch(
        &mut self,
        mut _stream: Http3OrWebTransportStream,
        _request_headers: &Vec<Header>,
        _request_body: Vec<u8>,
    ) {
        // do nothing
    }

    #[cfg(not(target_os = "android"))]
    fn maybe_process_response(&mut self) {
        let mut data_to_send = HashMap::new();
        self.response_to_send
            .retain(|id, receiver| match receiver.try_recv() {
                Ok((headers, body)) => {
                    data_to_send.insert(id.clone(), (headers.clone(), body.clone()));
                    false
                }
                Err(TryRecvError::Empty) => true,
                Err(TryRecvError::Disconnected) => false,
            });
        while let Some(stream) = data_to_send.keys().next().cloned() {
            let (header, data) = data_to_send.remove(&stream).unwrap();
            qtrace!("response headers: {:?}", header);
            match stream.send_headers(&header) {
                Ok(()) => {
                    self.new_response(stream, data);
                }
                _ => {}
            }
        }
    }
}

impl HttpServer for Http3ProxyServer {
    fn process_multiple(
        &mut self,
        dgram: Option<Datagram<&mut [u8]>>,
        now: Instant,
        max_datagrams: std::num::NonZeroUsize,
    ) -> OutputBatch {
        let output = self.server.process_multiple(dgram, now, max_datagrams);

        #[cfg(not(target_os = "android"))]
        let output = if self.response_to_send.is_empty() {
            output
        } else {
            // In case there are pending responses to send, make sure a reasonable
            // callback is returned.
            const MIN_INTERVAL: Duration = Duration::from_millis(100);

            match output {
                OutputBatch::None => OutputBatch::Callback(MIN_INTERVAL),
                o @ OutputBatch::DatagramBatch(_) => o,
                OutputBatch::Callback(d) => OutputBatch::Callback(min(d, MIN_INTERVAL)),
            }
        };

        output
    }

    fn process_events(&mut self, _now: Instant) {
        #[cfg(not(target_os = "android"))]
        self.maybe_process_response();
        while let Some(event) = self.server.next_event() {
            qtrace!("Event: {:?}", event);
            match event {
                Http3ServerEvent::Headers {
                    stream,
                    headers,
                    fin: _,
                } => {
                    qtrace!("Headers {:?}", headers);
                    if self.server_port != -1 {
                        let method_hdr = headers.iter().find(|&h| h.name() == ":method");
                        match method_hdr {
                            Some(method) => match method.value() {
                                "POST" => {
                                    let content_length =
                                        headers.iter().find(|&h| h.name() == "content-length");
                                    if let Some(length_str) = content_length {
                                        if let Ok(len) = length_str.value().parse::<u32>() {
                                            if len > 0 {
                                                self.requests.insert(stream, (headers, Vec::new()));
                                            } else {
                                                self.fetch(stream, &headers, b"".to_vec());
                                            }
                                        }
                                    }
                                }
                                _ => {
                                    self.fetch(stream, &headers, b"".to_vec());
                                }
                            },
                            _ => {}
                        }
                    } else {
                        let path_hdr = headers.iter().find(|&h| h.name() == ":path");
                        match path_hdr {
                            Some(ph) if !ph.value().is_empty() => {
                                let path = ph.value();
                                match &path[..6] {
                                    "/port?" => {
                                        let port = path[6..].parse::<i32>();
                                        if let Ok(port) = port {
                                            qtrace!("got port {}", port);
                                            self.server_port = port;
                                        }
                                    }
                                    _ => {}
                                }
                            }
                            _ => {}
                        }
                        stream
                            .send_headers(&[
                                Header::new(":status", "200"),
                                Header::new("cache-control", "no-cache"),
                                Header::new("content-length", "0"),
                            ])
                            .unwrap();
                    }
                }
                Http3ServerEvent::Data {
                    stream,
                    mut data,
                    fin,
                } => {
                    if let Some((_, body)) = self.requests.get_mut(&stream) {
                        body.append(&mut data);
                    }
                    if fin {
                        if let Some((headers, body)) = self.requests.remove(&stream) {
                            self.fetch(stream, &headers, body);
                        }
                    }
                }
                Http3ServerEvent::DataWritable { stream } => self.handle_stream_writable(stream),
                Http3ServerEvent::StateChange { .. } | Http3ServerEvent::PriorityUpdate { .. } => {}
                Http3ServerEvent::StreamReset { stream, error } => {
                    qtrace!("Http3ServerEvent::StreamReset {:?} {:?}", stream, error);
                }
                Http3ServerEvent::StreamStopSending { stream, error } => {
                    qtrace!(
                        "Http3ServerEvent::StreamStopSending {:?} {:?}",
                        stream,
                        error
                    );
                }
                Http3ServerEvent::WebTransport(_) => {}
            }
        }
    }

    fn has_events(&self) -> bool {
        self.server.has_events()
    }
}

#[derive(Default)]
struct NonRespondingServer {}

impl ::std::fmt::Display for NonRespondingServer {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "NonRespondingServer")
    }
}

impl HttpServer for NonRespondingServer {
    fn process_multiple(
        &mut self,
        _dgram: Option<Datagram<&mut [u8]>>,
        _now: Instant,
        _max_datagrams: std::num::NonZeroUsize,
    ) -> OutputBatch {
        OutputBatch::None
    }

    fn process_events(&mut self, _now: Instant) {}

    fn has_events(&self) -> bool {
        false
    }
}
enum ServerType {
    Http3,
    Http3Fail,
    Http3NoResponse,
    Http3Ech,
    Http3Proxy,
}

fn new_runner(
    server_type: ServerType,
    port: u16,
) -> Result<(SocketAddr, Option<Vec<u8>>, Runner), io::Error> {
    let mut ech_config = None;
    let addr: SocketAddr = if cfg!(target_os = "windows") {
        format!("127.0.0.1:{}", port).parse().unwrap()
    } else {
        format!("[::]:{}", port).parse().unwrap()
    };

    let socket = match neqo_bin::udp::Socket::bind(&addr) {
        Err(err) => {
            eprintln!("Unable to bind UDP socket: {}", err);
            exit(1)
        }
        Ok(s) => s,
    };

    let local_addr = match socket.local_addr() {
        Err(err) => {
            eprintln!("Socket local address not bound: {}", err);
            exit(1)
        }
        Ok(s) => s,
    };

    let anti_replay = AntiReplay::new(Instant::now(), Duration::from_secs(10), 7, 14)
        .expect("unable to setup anti-replay");
    let cid_mgr = Rc::new(RefCell::new(RandomConnectionIdGenerator::new(10)));

    let server: Box<dyn HttpServer> = match server_type {
        ServerType::Http3 => Box::new(Http3TestServer::new(
            Http3Server::new(
                Instant::now(),
                &[" HTTP2 Test Cert"],
                PROTOCOLS,
                anti_replay,
                cid_mgr,
                Http3Parameters::default()
                    .max_table_size_encoder(MAX_TABLE_SIZE)
                    .max_table_size_decoder(MAX_TABLE_SIZE)
                    .max_blocked_streams(MAX_BLOCKED_STREAMS)
                    .webtransport(true)
                    .connection_parameters(ConnectionParameters::default().datagram_size(1200)),
                None,
            )
            .expect("We cannot make a server!"),
        )),
        ServerType::Http3Fail => Box::new(Server(
            neqo_transport::server::Server::new(
                Instant::now(),
                &[" HTTP2 Test Cert"],
                PROTOCOLS,
                anti_replay,
                Box::new(AllowZeroRtt {}),
                cid_mgr,
                ConnectionParameters::default(),
            )
            .expect("We cannot make a server!"),
        )),
        ServerType::Http3NoResponse => Box::new(NonRespondingServer::default()),
        ServerType::Http3Ech => {
            let mut server = Box::new(Http3TestServer::new(
                Http3Server::new(
                    Instant::now(),
                    &[" HTTP2 Test Cert"],
                    PROTOCOLS,
                    anti_replay,
                    cid_mgr,
                    Http3Parameters::default()
                        .max_table_size_encoder(MAX_TABLE_SIZE)
                        .max_table_size_decoder(MAX_TABLE_SIZE)
                        .max_blocked_streams(MAX_BLOCKED_STREAMS),
                    None,
                )
                .expect("We cannot make a server!"),
            ));
            let ref mut unboxed_server = (*server).server;
            let (sk, pk) = generate_ech_keys().unwrap();
            unboxed_server
                .enable_ech(ECH_CONFIG_ID, ECH_PUBLIC_NAME, &sk, &pk)
                .expect("unable to enable ech");
            ech_config = Some(Vec::from(unboxed_server.ech_config()));
            server
        }
        ServerType::Http3Proxy => {
            let server_config = if env::var("MOZ_HTTP3_MOCHITEST").is_ok() {
                ("mochitest-cert", 8888)
            } else {
                (" HTTP2 Test Cert", -1)
            };
            let server = Box::new(Http3ProxyServer::new(
                Http3Server::new(
                    Instant::now(),
                    &[server_config.0],
                    PROTOCOLS,
                    anti_replay,
                    cid_mgr,
                    Http3Parameters::default()
                        .max_table_size_encoder(MAX_TABLE_SIZE)
                        .max_table_size_decoder(MAX_TABLE_SIZE)
                        .max_blocked_streams(MAX_BLOCKED_STREAMS)
                        .webtransport(true)
                        .connection_parameters(ConnectionParameters::default().datagram_size(1200)),
                    None,
                )
                .expect("We cannot make a server!"),
                server_config.1,
            ));
            server
        }
    };

    Ok((
        local_addr,
        ech_config,
        Runner::new(Box::new(Instant::now), server, vec![(local_addr, socket)]),
    ))
}

#[tokio::main]
async fn main() -> Result<(), io::Error> {
    neqo_common::log::init(None);

    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Wrong arguments.");
        exit(1)
    }

    // Read data from stdin and terminate the server if EOF is detected, which
    // means that runxpcshelltests.py ended without shutting down the server.
    thread::spawn(|| loop {
        let mut buffer = String::new();
        match io::stdin().read_line(&mut buffer) {
            Ok(n) => {
                if n == 0 {
                    exit(0);
                }
            }
            Err(_) => {
                exit(0);
            }
        }
    });

    init_db(PathBuf::from(args[1].clone())).unwrap();

    let local = tokio::task::LocalSet::new();
    let mut hosts = vec![];
    let mut ech_config = None;

    let proxy_port = match env::var("MOZ_HTTP3_PROXY_PORT") {
        Ok(val) => val.parse::<u16>().unwrap(),
        _ => 0,
    };

    for (server_type, port) in [
        (ServerType::Http3, 0),
        (ServerType::Http3Fail, 0),
        (ServerType::Http3Ech, 0),
        (ServerType::Http3Proxy, proxy_port),
        (ServerType::Http3NoResponse, 0),
    ] {
        let (address, ech, runner) = new_runner(server_type, port)?;
        hosts.push(address);
        if let Some(ech) = ech {
            ech_config = Some(ech);
        }

        local.spawn_local(runner.run());
    }

    // Note this is parsed by test runner.
    // https://searchfox.org/mozilla-central/rev/e69f323af80c357d287fb6314745e75c62eab92a/testing/mozbase/mozserve/mozserve/servers.py#116-121
    println!(
        "HTTP3 server listening on ports {}, {}, {}, {} and {}. EchConfig is @{}@",
        hosts[0].port(),
        hosts[1].port(),
        hosts[2].port(),
        hosts[3].port(),
        hosts[4].port(),
        BASE64_STANDARD.encode(&ech_config.unwrap())
    );

    local.await;

    Ok(())
}

#[no_mangle]
extern "C" fn __tsan_default_suppressions() -> *const std::os::raw::c_char {
    // https://github.com/rust-lang/rust/issues/128769
    "race:tokio::runtime::io::registration_set::RegistrationSet::allocate\0".as_ptr() as *const _
}

// Work around until we can use raw-dylibs.
#[cfg_attr(target_os = "windows", link(name = "runtimeobject"))]
extern "C" {}
#[cfg_attr(target_os = "windows", link(name = "propsys"))]
extern "C" {}
#[cfg_attr(target_os = "windows", link(name = "iphlpapi"))]
extern "C" {}
