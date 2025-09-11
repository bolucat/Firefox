/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.reviewprompt

import mozilla.components.support.test.ext.joinBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.nimbus.FakeNimbusEventStore
import org.mozilla.fenix.reviewprompt.ReviewPromptState.Eligible.Type

class ReviewPromptMiddlewareTest {

    private val eventStore = FakeNimbusEventStore()

    private var isTelemetryEnabled = true
    private lateinit var mainCriteria: Sequence<Boolean>
    private lateinit var subCriteria: Sequence<Boolean>

    private val store = AppStore(
        middlewares = listOf(
            ReviewPromptMiddleware(
                isReviewPromptFeatureEnabled = { true },
                isTelemetryEnabled = { isTelemetryEnabled },
                createJexlHelper = {
                    object : NimbusMessagingHelperInterface {
                        override fun evalJexl(expression: String) = assertUnused()
                        override fun getUuid(template: String) = assertUnused()
                        override fun stringFormat(template: String, uuid: String?) = assertUnused()
                    }
                },
                buildTriggerMainCriteria = { mainCriteria },
                buildTriggerSubCriteria = { subCriteria },
                nimbusEventStore = eventStore,
            ),
        ),
    )

    @Test
    fun `GIVEN main criteria satisfied AND one of sub-criteria satisfied WHEN check requested THEN sets eligible`() {
        mainCriteria = sequenceOf(true)
        subCriteria = sequenceOf(false, true, false)

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertTrue(store.state.reviewPrompt is ReviewPromptState.Eligible)
    }

    @Test
    fun `GIVEN main criteria satisfied AND first sub-criteria satisfied WHEN check requested THEN other sub-criteria are not checked`() {
        mainCriteria = sequenceOf(true)
        var continuedPastFirstSatisfied = false
        subCriteria = sequence {
            yield(true)
            continuedPastFirstSatisfied = true
            yield(true)
        }

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertFalse(continuedPastFirstSatisfied)
    }

    @Test
    fun `GIVEN no main criteria AND one of sub-criteria satisfied WHEN check requested THEN sets eligible`() {
        mainCriteria = emptySequence()
        subCriteria = sequenceOf(false, true, false)

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertTrue(store.state.reviewPrompt is ReviewPromptState.Eligible)
    }

    @Test
    fun `GIVEN main criteria satisfied AND no sub-criteria satisfied WHEN check requested THEN sets not eligible`() {
        mainCriteria = sequenceOf(true)
        subCriteria = sequenceOf(false)

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(
            AppState(reviewPrompt = ReviewPromptState.NotEligible),
            store.state,
        )
    }

    @Test
    fun `GIVEN main criteria satisfied AND no sub-criteria WHEN check requested THEN sets not eligible`() {
        mainCriteria = sequenceOf(true)
        subCriteria = emptySequence()

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(
            AppState(reviewPrompt = ReviewPromptState.NotEligible),
            store.state,
        )
    }

    @Test
    fun `GIVEN one of main criteria not satisfied AND sub-criteria satisfied WHEN check requested THEN sets not eligible`() {
        mainCriteria = sequenceOf(true, false, true)
        subCriteria = sequenceOf(true)

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(
            AppState(reviewPrompt = ReviewPromptState.NotEligible),
            store.state,
        )
    }

    @Test
    fun `GIVEN one of main criteria not satisfied WHEN check requested THEN other criteria not checked`() {
        var continuedPastFirstNotSatisfied = false
        mainCriteria = sequence {
            yield(false)
            continuedPastFirstNotSatisfied = true
            yield(false)
        }
        subCriteria = sequence {
            continuedPastFirstNotSatisfied = true
            yield(false)
        }

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertFalse(continuedPastFirstNotSatisfied)
    }

    @Test
    fun `GIVEN check ran WHEN check requested again THEN does nothing`() {
        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()
        val expectedState = store.state

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(expectedState, store.state)
    }

    @Test
    fun `GIVEN review prompt shown WHEN check requested THEN does nothing`() {
        store.dispatch(ReviewPromptAction.ReviewPromptShown).joinBlocking()
        val expectedState = store.state

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(expectedState, store.state)
    }

    @Test
    fun `WHEN review prompt shown THEN an event is recorded`() {
        store.dispatch(ReviewPromptAction.ReviewPromptShown).joinBlocking()

        eventStore.assertSingleEventEquals("review_prompt_shown")
    }

    @Test
    fun `WHEN don't show review prompt THEN does nothing`() {
        assertNoOp(ReviewPromptAction.DoNotShowReviewPrompt)
    }

    @Test
    fun `WHEN show custom prompt THEN does nothing`() {
        assertNoOp(ReviewPromptAction.ShowCustomReviewPrompt)
    }

    @Test
    fun `WHEN show Play Store prompt THEN does nothing`() {
        assertNoOp(ReviewPromptAction.ShowPlayStorePrompt)
    }

    @Test
    fun `GIVEN telemetry enabled AND criteria satisfied WHEN check requested THEN sets eligible for Custom prompt`() {
        isTelemetryEnabled = true
        mainCriteria = sequenceOf(true)
        subCriteria = sequenceOf(true)

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(
            AppState(reviewPrompt = ReviewPromptState.Eligible(Type.Custom)),
            store.state,
        )
    }

    @Test
    fun `GIVEN telemetry disabled AND criteria satisfied WHEN check requested THEN sets eligible for Play Store prompt`() {
        isTelemetryEnabled = false
        mainCriteria = sequenceOf(true)
        subCriteria = sequenceOf(true)

        store.dispatch(ReviewPromptAction.CheckIfEligibleForReviewPrompt).joinBlocking()

        assertEquals(
            AppState(reviewPrompt = ReviewPromptState.Eligible(Type.PlayStore)),
            store.state,
        )
    }

    @Test
    fun `WHEN evalJexl returns false THEN createdAtLeastOneBookmark returns false`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = false)

        val result = createdAtLeastOneBookmark(jexlHelper)

        assertFalse(result)
    }

    @Test
    fun `WHEN evalJexl returns true THEN createdAtLeastOneBookmark returns true`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = true)

        val result = createdAtLeastOneBookmark(jexlHelper)

        assertTrue(result)
    }

    @Test
    fun `WHEN evalJexl returns false THEN isDefaultBrowser returns false`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = false)

        val result = isDefaultBrowser(jexlHelper)

        assertFalse(result)
    }

    @Test
    fun `WHEN evalJexl returns true THEN isDefaultBrowser returns true`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = true)

        val result = isDefaultBrowser(jexlHelper)

        assertTrue(result)
    }

    @Test
    fun `WHEN evalJexl returns false THEN usedAppOnAtLeastFourOfLastSevenDays returns false`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = false)

        val result = usedAppOnAtLeastFourOfLastSevenDays(jexlHelper)

        assertFalse(result)
    }

    @Test
    fun `WHEN evalJexl returns true THEN usedAppOnAtLeastFourOfLastSevenDays returns true`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = true)

        val result = usedAppOnAtLeastFourOfLastSevenDays(jexlHelper)

        assertTrue(result)
    }

    @Test
    fun `WHEN evalJexl returns false THEN hasNotBeenPromptedLastFourMonths returns false`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = false)

        val result = hasNotBeenPromptedLastFourMonths(jexlHelper)

        assertFalse(result)
    }

    @Test
    fun `WHEN evalJexl returns true THEN hasNotBeenPromptedLastFourMonths returns true`() {
        val jexlHelper = FakeNimbusMessagingHelperInterface(evalJexlValue = true)

        val result = hasNotBeenPromptedLastFourMonths(jexlHelper)

        assertTrue(result)
    }

    private fun assertNoOp(action: ReviewPromptAction) {
        val withoutMiddleware = AppStore()
        withoutMiddleware.dispatch(action).joinBlocking()
        val expectedState = withoutMiddleware.state

        store.dispatch(action).joinBlocking()

        assertEquals(
            expectedState,
            store.state,
        )
    }

    private fun assertUnused(): Nothing =
        throw AssertionError("Expected unused function, but was called here ")

    private class FakeNimbusMessagingHelperInterface(val evalJexlValue: Boolean) :
        NimbusMessagingHelperInterface {
        override fun evalJexl(expression: String): Boolean = evalJexlValue
        override fun getUuid(template: String): String? = null
        override fun stringFormat(template: String, uuid: String?): String = ""
    }
}
