/* eslint-env mozilla/chrome-script */

// Just receive 'foo' message and forward it back
// as 'bar' message
addMessageListener("foo", function (message) {
  sendAsyncMessage("bar", message);
});

addMessageListener("valid-assert", function () {
  assert.ok(true, "valid assertion");
  assert.equal(1, 1, "another valid assertion");
  sendAsyncMessage("valid-assert-done");
});

addMessageListener("sync-message", () => {
  return "Received a synchronous message.";
});
