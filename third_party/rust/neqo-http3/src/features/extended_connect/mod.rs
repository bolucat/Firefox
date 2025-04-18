// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

pub(crate) mod webtransport_session;
pub(crate) mod webtransport_streams;

use std::fmt::Debug;

use neqo_common::Header;
use neqo_transport::{AppError, StreamId};
pub(crate) use webtransport_session::WebTransportSession;

use crate::{
    client_events::Http3ClientEvents,
    features::NegotiationState,
    settings::{HSettingType, HSettings},
    CloseType, Http3StreamInfo, Http3StreamType, Res,
};

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum SessionCloseReason {
    Error(AppError),
    Status(u16),
    Clean { error: u32, message: String },
}

impl From<CloseType> for SessionCloseReason {
    fn from(close_type: CloseType) -> Self {
        match close_type {
            CloseType::ResetApp(e) | CloseType::ResetRemote(e) | CloseType::LocalError(e) => {
                Self::Error(e)
            }
            CloseType::Done => Self::Clean {
                error: 0,
                message: String::new(),
            },
        }
    }
}

pub(crate) trait ExtendedConnectEvents: Debug {
    fn session_start(
        &self,
        connect_type: ExtendedConnectType,
        stream_id: StreamId,
        status: u16,
        headers: Vec<Header>,
    );
    fn session_end(
        &self,
        connect_type: ExtendedConnectType,
        stream_id: StreamId,
        reason: SessionCloseReason,
        headers: Option<Vec<Header>>,
    );
    fn extended_connect_new_stream(&self, stream_info: Http3StreamInfo) -> Res<()>;
    fn new_datagram(&self, session_id: StreamId, datagram: Vec<u8>);
}

#[derive(Debug, PartialEq, Copy, Clone, Eq)]
pub(crate) enum ExtendedConnectType {
    WebTransport,
}

impl ExtendedConnectType {
    #[must_use]
    #[expect(
        clippy::unused_self,
        reason = "This will change when we have more features using ExtendedConnectType."
    )]
    pub const fn string(self) -> &'static str {
        "webtransport"
    }

    #[expect(
        clippy::unused_self,
        reason = "This will change when we have more features using ExtendedConnectType."
    )]
    #[must_use]
    pub const fn get_stream_type(self, session_id: StreamId) -> Http3StreamType {
        Http3StreamType::WebTransport(session_id)
    }
}

impl From<ExtendedConnectType> for HSettingType {
    fn from(_type: ExtendedConnectType) -> Self {
        // This will change when we have more features using ExtendedConnectType.
        Self::EnableWebTransport
    }
}

#[derive(Debug)]
pub(crate) struct ExtendedConnectFeature {
    feature_negotiation: NegotiationState,
}

impl ExtendedConnectFeature {
    #[must_use]
    pub fn new(connect_type: ExtendedConnectType, enable: bool) -> Self {
        Self {
            feature_negotiation: NegotiationState::new(enable, HSettingType::from(connect_type)),
        }
    }

    pub fn set_listener(&mut self, new_listener: Http3ClientEvents) {
        self.feature_negotiation.set_listener(new_listener);
    }

    pub fn handle_settings(&mut self, settings: &HSettings) {
        self.feature_negotiation.handle_settings(settings);
    }

    #[must_use]
    pub const fn enabled(&self) -> bool {
        self.feature_negotiation.enabled()
    }
}
#[cfg(test)]
mod tests;
