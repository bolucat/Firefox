/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function looksLikeOrigin() {
  let tests = [
    {
      token: "",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "user@example.com",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "user:pass@example.com",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.USERINFO_OR_PORT,
    },

    {
      token: "example.com:1234",
      // This should be `USERINFO_OR_PORT`, but it matches
      // `REGEXP_LIKE_PROTOCOL`, so it returns `NONE`.
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },
    {
      token: "example.com:1234",
      args: { noPort: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "user@example.com:1234",
      // This should be `USERINFO_OR_PORT`, but it matches
      // `REGEXP_LIKE_PROTOCOL`, so it returns `NONE`.
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },
    {
      token: "user@example.com:1234",
      args: { noPort: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "user:pass@example.com:1234",
      // This should be `USERINFO_OR_PORT`, but it matches
      // `REGEXP_LIKE_PROTOCOL`, so it returns `NONE`.
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },
    {
      token: "user:pass@example.com:1234",
      args: { noPort: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "1.2.3.4",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.IP,
    },
    {
      token: "1.2.3.4",
      args: { noIp: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "[2001:0db8:0000:0000:0000:8a2e:0370:7334]",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.IP,
    },
    {
      token: "[2001:0db8:0000:0000:0000:8a2e:0370:7334]",
      args: { noIp: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "[2001:db8::8a2e:370:7334]",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.IP,
    },
    {
      token: "[2001:db8::8a2e:370:7334]",
      args: { noIp: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "a!@#$%^&( z",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "example",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.NONE,
    },

    {
      token: "localhost",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.KNOWN_DOMAIN,
    },
    {
      token: "localhost",
      args: { ignoreKnownDomains: true },
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.OTHER,
    },

    {
      token: "example.com",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.OTHER,
    },
    {
      token: "example.co",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.OTHER,
    },
    {
      token: "example.c",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.OTHER,
    },
    {
      token: "example.",
      expected: UrlbarTokenizer.LOOKS_LIKE_ORIGIN.OTHER,
    },
  ];

  for (let { token, args, expected } of tests) {
    Assert.strictEqual(
      UrlbarTokenizer.looksLikeOrigin(token, args),
      expected,
      "looksLikeOrigin should return expected value: " +
        JSON.stringify({ token, args })
    );
  }
});

add_task(async function test_tokenizer() {
  let testContexts = [
    { desc: "Empty string", searchString: "", expectedTokens: [] },
    { desc: "Spaces string", searchString: "      ", expectedTokens: [] },
    {
      desc: "Single word string",
      searchString: "test",
      expectedTokens: [{ value: "test", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "Multi word string with mixed whitespace types",
      searchString: " test1 test2\u1680test3\u2004test4\u1680",
      expectedTokens: [
        { value: "test1", type: UrlbarTokenizer.TYPE.TEXT },
        { value: "test2", type: UrlbarTokenizer.TYPE.TEXT },
        { value: "test3", type: UrlbarTokenizer.TYPE.TEXT },
        { value: "test4", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "separate restriction char at beginning",
      searchString: `${UrlbarTokenizer.RESTRICT.BOOKMARK} test`,
      expectedTokens: [
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.RESTRICT_BOOKMARK,
        },
        { value: "test", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "do not separate restriction char at beginning in search mode",
      searchMode: { engineName: "testEngine" },
      searchString: `${UrlbarTokenizer.RESTRICT.SEARCH}test`,
      expectedTokens: [{ value: "?test", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "separate restriction char at end",
      searchString: `test ${UrlbarTokenizer.RESTRICT.BOOKMARK}`,
      expectedTokens: [
        { value: "test", type: UrlbarTokenizer.TYPE.TEXT },
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.RESTRICT_BOOKMARK,
        },
      ],
    },
    {
      desc: "boundary restriction char at end",
      searchString: `test${UrlbarTokenizer.RESTRICT.BOOKMARK}`,
      expectedTokens: [
        {
          value: `test${UrlbarTokenizer.RESTRICT.BOOKMARK}`,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
      ],
    },
    {
      desc: "do not separate boundary search restriction char at end",
      searchString: `test${UrlbarTokenizer.RESTRICT.SEARCH}`,
      expectedTokens: [{ value: "test?", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "separate restriction char in the middle",
      searchString: `test ${UrlbarTokenizer.RESTRICT.BOOKMARK} test`,
      expectedTokens: [
        { value: "test", type: UrlbarTokenizer.TYPE.TEXT },
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
        { value: "test", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "restriction char in the middle",
      searchString: `test${UrlbarTokenizer.RESTRICT.BOOKMARK}test`,
      expectedTokens: [
        {
          value: `test${UrlbarTokenizer.RESTRICT.BOOKMARK}test`,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
      ],
    },
    {
      desc: "restriction char in the middle 2",
      searchString: `test${UrlbarTokenizer.RESTRICT.BOOKMARK} test`,
      expectedTokens: [
        {
          value: `test${UrlbarTokenizer.RESTRICT.BOOKMARK}`,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
        { value: `test`, type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "double boundary restriction char",
      searchString: `${UrlbarTokenizer.RESTRICT.BOOKMARK}test${UrlbarTokenizer.RESTRICT.TITLE}`,
      expectedTokens: [
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.RESTRICT_BOOKMARK,
        },
        {
          value: `test${UrlbarTokenizer.RESTRICT.TITLE}`,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
      ],
    },
    {
      desc: "do not separate boundary search restriction char at end when using using a double non-combinable restriction char with a single-character string",
      searchString: `t${UrlbarTokenizer.RESTRICT.BOOKMARK}${UrlbarTokenizer.RESTRICT.SEARCH}`,
      expectedTokens: [
        {
          value: `t${UrlbarTokenizer.RESTRICT.BOOKMARK}${UrlbarTokenizer.RESTRICT.SEARCH}`,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
      ],
    },
    {
      desc: "only boundary restriction chars",
      searchString: `${UrlbarTokenizer.RESTRICT.BOOKMARK}${UrlbarTokenizer.RESTRICT.TITLE}`,
      expectedTokens: [
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.RESTRICT_BOOKMARK,
        },
        {
          value: UrlbarTokenizer.RESTRICT.TITLE,
          type: UrlbarTokenizer.TYPE.RESTRICT_TITLE,
        },
      ],
    },
    {
      desc: "only the boundary restriction char",
      searchString: UrlbarTokenizer.RESTRICT.BOOKMARK,
      expectedTokens: [
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.RESTRICT_BOOKMARK,
        },
      ],
    },
    // Some restriction chars may be # or ?, that are also valid path parts.
    // The next 2 tests will check we consider those as part of url paths.
    {
      desc: "boundary # char on path",
      searchString: "test/#",
      expectedTokens: [
        { value: "test/#", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "boundary ? char on path",
      searchString: "test/?",
      expectedTokens: [
        { value: "test/?", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "multiple boundary restriction chars suffix",
      searchString: `test ${UrlbarTokenizer.RESTRICT.HISTORY} ${UrlbarTokenizer.RESTRICT.TAG}`,
      expectedTokens: [
        { value: "test", type: UrlbarTokenizer.TYPE.TEXT },
        {
          value: UrlbarTokenizer.RESTRICT.HISTORY,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
        {
          value: UrlbarTokenizer.RESTRICT.TAG,
          type: UrlbarTokenizer.TYPE.RESTRICT_TAG,
        },
      ],
    },
    {
      desc: "multiple boundary restriction chars prefix",
      searchString: `${UrlbarTokenizer.RESTRICT.HISTORY} ${UrlbarTokenizer.RESTRICT.TAG} test`,
      expectedTokens: [
        {
          value: UrlbarTokenizer.RESTRICT.HISTORY,
          type: UrlbarTokenizer.TYPE.RESTRICT_HISTORY,
        },
        {
          value: UrlbarTokenizer.RESTRICT.TAG,
          type: UrlbarTokenizer.TYPE.TEXT,
        },
        { value: "test", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "Math with division",
      searchString: "3.6/1.2",
      expectedTokens: [{ value: "3.6/1.2", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "ipv4 in bookmarks",
      searchString: `${UrlbarTokenizer.RESTRICT.BOOKMARK} 192.168.1.1:8`,
      expectedTokens: [
        {
          value: UrlbarTokenizer.RESTRICT.BOOKMARK,
          type: UrlbarTokenizer.TYPE.RESTRICT_BOOKMARK,
        },
        { value: "192.168.1.1:8", type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "email",
      searchString: "test@mozilla.com",
      expectedTokens: [
        { value: "test@mozilla.com", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "email2",
      searchString: "test.test@mozilla.co.uk",
      expectedTokens: [
        { value: "test.test@mozilla.co.uk", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "protocol",
      searchString: "http://test",
      expectedTokens: [
        { value: "http://test", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "bogus protocol with host (we allow visits to http://///example.com)",
      searchString: "http:///test",
      expectedTokens: [
        { value: "http:///test", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "file protocol with path",
      searchString: "file:///home",
      expectedTokens: [
        { value: "file:///home", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "almost a protocol",
      searchString: "http:",
      expectedTokens: [
        { value: "http:", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "almost a protocol 2",
      searchString: "http:/",
      expectedTokens: [
        { value: "http:/", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "bogus protocol (we allow visits to http://///example.com)",
      searchString: "http:///",
      expectedTokens: [
        { value: "http:///", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "file protocol",
      searchString: "file:///",
      expectedTokens: [
        { value: "file:///", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "userinfo",
      searchString: "user:pass@test",
      expectedTokens: [
        { value: "user:pass@test", type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "domain with two dots",
      searchString: "www.mozilla.org",
      expectedTokens: [
        {
          value: "www.mozilla.org",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "domain with two dots and allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "www.mozilla.org",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "www.mozilla.org",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "domain with one dot",
      searchString: "mozilla.org",
      expectedTokens: [
        {
          value: "mozilla.org",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "domain with one dot and allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "mozilla.org",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "mozilla.org",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "looks like simple origin",
      searchString: "mozilla.o",
      expectedTokens: [
        {
          value: "mozilla.o",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "looks like simple origin with allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "mozilla.o",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "mozilla.o",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "query ends with dot",
      searchString: "mozilla.",
      expectedTokens: [
        {
          value: "mozilla.",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "query ends with dot with allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "mozilla.",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "mozilla.",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "data uri",
      searchString: "data:text/plain,Content",
      expectedTokens: [
        {
          value: "data:text/plain,Content",
          type: UrlbarTokenizer.TYPE.POSSIBLE_URL,
        },
      ],
    },
    {
      desc: "ipv6",
      searchString: "[2001:db8::1]",
      expectedTokens: [
        { value: "[2001:db8::1]", type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "numeric domain",
      searchString: "test1001.com",
      expectedTokens: [
        {
          value: "test1001.com",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "invalid ip",
      searchString: "192.2134.1.2",
      expectedTokens: [
        { value: "192.2134.1.2", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "ipv4",
      searchString: "1.2.3.4",
      expectedTokens: [
        { value: "1.2.3.4", type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "host/path",
      searchString: "test/test",
      expectedTokens: [
        { value: "test/test", type: UrlbarTokenizer.TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "percent encoded string",
      searchString: "%E6%97%A5%E6%9C%AC",
      expectedTokens: [
        { value: "%E6%97%A5%E6%9C%AC", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "Uppercase",
      searchString: "TEST",
      expectedTokens: [{ value: "TEST", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "Mixed case 1",
      searchString: "TeSt",
      expectedTokens: [{ value: "TeSt", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "Mixed case 2",
      searchString: "tEsT",
      expectedTokens: [{ value: "tEsT", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "Uppercase with spaces",
      searchString: "TEST EXAMPLE",
      expectedTokens: [
        { value: "TEST", type: UrlbarTokenizer.TYPE.TEXT },
        { value: "EXAMPLE", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "Mixed case with spaces",
      searchString: "TeSt eXaMpLe",
      expectedTokens: [
        { value: "TeSt", type: UrlbarTokenizer.TYPE.TEXT },
        { value: "eXaMpLe", type: UrlbarTokenizer.TYPE.TEXT },
      ],
    },
    {
      desc: "plain number",
      searchString: "1001",
      expectedTokens: [{ value: "1001", type: UrlbarTokenizer.TYPE.TEXT }],
    },
    {
      desc: "data uri with spaces",
      searchString: "data:text/html,oh hi?",
      expectedTokens: [
        {
          value: "data:text/html,oh hi?",
          type: UrlbarTokenizer.TYPE.POSSIBLE_URL,
        },
      ],
    },
    {
      desc: "data uri with spaces ignored with other tokens",
      searchString: "hi data:text/html,oh hi?",
      expectedTokens: [
        {
          value: "hi",
          type: UrlbarTokenizer.TYPE.TEXT,
        },
        {
          value: "data:text/html,oh",
          type: UrlbarTokenizer.TYPE.POSSIBLE_URL,
        },
        {
          value: "hi?",
          type: UrlbarTokenizer.TYPE.TEXT,
        },
      ],
    },
    {
      desc: "whitelisted host",
      searchString: "test whitelisted",
      expectedTokens: [
        {
          value: "test",
          type: UrlbarTokenizer.TYPE.TEXT,
        },
        {
          value: "whitelisted",
          type: UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
  ];

  Services.prefs.setBoolPref("browser.fixup.domainwhitelist.whitelisted", true);

  for (let queryContext of testContexts) {
    info(queryContext.desc);
    queryContext.trimmedSearchString = queryContext.searchString.trim();
    for (let token of queryContext.expectedTokens) {
      token.lowerCaseValue = token.value.toLocaleLowerCase();
    }

    if (queryContext.hasOwnProperty("allowSearchSuggestionsForSimpleOrigins")) {
      Services.prefs.setBoolPref(
        "browser.urlbar.allowSearchSuggestionsForSimpleOrigins",
        queryContext.allowSearchSuggestionsForSimpleOrigins
      );
    }

    let newQueryContext = UrlbarTokenizer.tokenize(queryContext);
    Assert.equal(
      queryContext,
      newQueryContext,
      "The queryContext object is the same"
    );
    Assert.deepEqual(
      queryContext.tokens,
      queryContext.expectedTokens,
      "Check the expected tokens"
    );

    Services.prefs.clearUserPref(
      "browser.urlbar.allowSearchSuggestionsForSimpleOrigins"
    );
  }
});
