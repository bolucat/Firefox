import pytest
from webdriver.bidi.error import UnknownErrorException

# This site fails with a redirect loop with a Firefox UA

URL = "https://fanplus.co.jp/feature/artistarena"
REDIR_FAILURE_CSS = "[data-l10n-id=redirectLoop-title]"
REDIR_FAILURE_TEXT = "ERROR_REDIRECT_LOOP"
SUCCESS_CSS = "#globaltop_header"


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(SUCCESS_CSS)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    # We will either get a static error page with the text, or
    # an UnknownErrorException from WebDriver.
    try:
        await client.navigate(URL, wait="none", no_skip=True)
        desktop, mobile = client.await_first_element_of(
            [client.css(REDIR_FAILURE_CSS), client.text(REDIR_FAILURE_TEXT)],
            is_displayed=True,
            timeout=30,
        )
        assert desktop or mobile
    except UnknownErrorException:
        assert True
