import pytest

pytestmark = pytest.mark.asyncio

pytest_plugins = "tests.bidi.emulation.set_locale_override.conftest"


@pytest.mark.capabilities(
    {
        "moz:firefoxOptions": {
            "prefs": {
                "dom.ipc.processCount": 1,
            },
        },
    }
)
async def test_locale_override_isolated_in_browsing_context(
    bidi_session, get_current_locale, some_locale, another_locale
):
    context_in_process_1 = await bidi_session.browsing_context.create(type_hint="tab")

    await bidi_session.emulation.set_locale_override(
        contexts=[context_in_process_1["context"]], locale=some_locale
    )

    # Create one more context which should share the process
    # with the previously created context.
    context_in_process_2 = await bidi_session.browsing_context.create(type_hint="tab")
    await bidi_session.emulation.set_locale_override(
        contexts=[context_in_process_2["context"]], locale=another_locale
    )

    # Make sure that the locale override didn't override inappropriate context.
    assert await get_current_locale(context_in_process_1) == some_locale
    assert await get_current_locale(context_in_process_2) == another_locale
