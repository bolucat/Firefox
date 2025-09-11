/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.reviewprompt

import androidx.annotation.VisibleForTesting
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.service.nimbus.evalJexlSafe
import mozilla.components.service.nimbus.messaging.use
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.CheckIfEligibleForReviewPrompt
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.DoNotShowReviewPrompt
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.ReviewPromptShown
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.ShowCustomReviewPrompt
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.ShowPlayStorePrompt
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.messaging.CustomAttributeProvider

private const val REVIEW_PROMPT_SHOWN_NIMBUS_EVENT_ID = "review_prompt_shown"

/**
 * [Middleware] evaluating the triggers to show a review prompt.
 *
 * @param isReviewPromptFeatureEnabled If returns false disables the entire feature and prompt will never be shown.
 * @param isTelemetryEnabled Returns true if the user allows sending technical and interaction telemetry.
 * @param createJexlHelper Returns a helper for evaluating JEXL expressions.
 * @param buildTriggerMainCriteria Builds a sequence of trigger's main criteria that all need to be true.
 * @param buildTriggerSubCriteria Builds a sequence of trigger's sub-criteria.
 * Only one of these needs to be true (in addition to the main criteria).
 * @param nimbusEventStore [NimbusEventStore] used to record events evaluated in JEXL expressions.
 */
class ReviewPromptMiddleware(
    private val isReviewPromptFeatureEnabled: () -> Boolean,
    private val isTelemetryEnabled: () -> Boolean,
    private val createJexlHelper: () -> NimbusMessagingHelperInterface,
    private val buildTriggerMainCriteria: (NimbusMessagingHelperInterface) -> Sequence<Boolean> =
        TiggerBuilder.mainCriteria(isReviewPromptFeatureEnabled),
    private val buildTriggerSubCriteria: (NimbusMessagingHelperInterface) -> Sequence<Boolean> =
        TiggerBuilder::subCriteria,
    private val nimbusEventStore: NimbusEventStore,
) : Middleware<AppState, AppAction> {

    private object TiggerBuilder {
        fun mainCriteria(
            isReviewPromptEnabled: () -> Boolean,
        ): (NimbusMessagingHelperInterface) -> Sequence<Boolean> = { jexlHelper ->
            sequence {
                yield(isReviewPromptEnabled())
                yield(hasNotBeenPromptedLastFourMonths(jexlHelper))
                yield(usedAppOnAtLeastFourOfLastSevenDays(jexlHelper))
            }
        }

        fun subCriteria(jexlHelper: NimbusMessagingHelperInterface): Sequence<Boolean> {
            return sequence {
                yield(createdAtLeastOneBookmark(jexlHelper))
                yield(isDefaultBrowser(jexlHelper))
            }
        }
    }

    override fun invoke(
        context: MiddlewareContext<AppState, AppAction>,
        next: (AppAction) -> Unit,
        action: AppAction,
    ) {
        if (action !is AppAction.ReviewPromptAction) {
            next(action)
            return
        }

        when (action) {
            CheckIfEligibleForReviewPrompt -> handleReviewPromptCheck(context)
            ReviewPromptShown -> nimbusEventStore.recordEvent(REVIEW_PROMPT_SHOWN_NIMBUS_EVENT_ID)
            DoNotShowReviewPrompt -> Unit
            ShowCustomReviewPrompt -> Unit
            ShowPlayStorePrompt -> Unit
        }

        next(action)
    }

    private fun handleReviewPromptCheck(context: MiddlewareContext<AppState, AppAction>) {
        if (context.state.reviewPrompt != ReviewPromptState.Unknown) {
            // We only want to try to show it once to avoid unnecessary disk reads.
            return
        }

        createJexlHelper().use { jexlHelper ->
            val allMainCriteriaSatisfied = buildTriggerMainCriteria(jexlHelper).all { it }
            if (!allMainCriteriaSatisfied) {
                context.dispatch(DoNotShowReviewPrompt)
                return@use
            }

            val atLeastOneOfSubCriteriaSatisfied = buildTriggerSubCriteria(jexlHelper).any { it }
            if (atLeastOneOfSubCriteriaSatisfied) {
                if (isTelemetryEnabled()) {
                    context.dispatch(ShowCustomReviewPrompt)
                } else {
                    context.dispatch(ShowPlayStorePrompt)
                }
            } else {
                context.dispatch(DoNotShowReviewPrompt)
            }
        }
    }
}

/**
 * Checks that the prompt hasn't been shown in the last 4 months
 * to avoid hitting the Play In-App Review API quota.
 *
 * Implementation note:
 * A month is tracked in Nimbus as a 28-day bucket, so converting it to 4 weeks doesn't change the semantics.
 * Because of how buckets are advanced, using a monthly bucket can be almost an entire month off, so using weeks
 * gives us better precision (means we might wait for 4 months and almost a week, instead of almost 5 months).
 * This is why I opted for 16 weeks instead of 4 months.
 *
 * See:
 * [Nimbus Bucket Advancement & Retention docs](https://experimenter.info/mobile-behavioral-targeting/#bucket-advancement--retention)
 */
fun hasNotBeenPromptedLastFourMonths(jexlHelper: NimbusMessagingHelperInterface): Boolean {
    return jexlHelper.evalJexlSafe("'$REVIEW_PROMPT_SHOWN_NIMBUS_EVENT_ID'|eventLastSeen('Weeks') > 16")
}

/**
 * Evaluates whether the user has created at least one bookmark.
 *
 * Note: Because Nimbus limits data to 4 calendar years, this will ignore bookmarks created before then.
 */
@VisibleForTesting
internal fun createdAtLeastOneBookmark(jexlHelper: NimbusMessagingHelperInterface): Boolean {
    return jexlHelper.evalJexlSafe("'bookmark_added'|eventSum('Years', 4) >= 1")
}

/**
 * The raw string "is_default_browser" must match the json value provided in
 * [CustomAttributeProvider.getCustomAttributes].
 */
@VisibleForTesting
internal fun isDefaultBrowser(jexlHelper: NimbusMessagingHelperInterface) =
    jexlHelper.evalJexlSafe("is_default_browser")

/**
 * Evaluates whether the user has used the app on at least 4 distinct days
 * within the last 7 days. This does not require consecutive days.
 *
 * @return true if the user has opened the app on 4 or more days in the last 7, false otherwise
 */
@VisibleForTesting
internal fun usedAppOnAtLeastFourOfLastSevenDays(
    jexlHelper: NimbusMessagingHelperInterface,
): Boolean {
    return jexlHelper.evalJexlSafe("'app_opened'|eventCountNonZero('Days', 7) >= 4")
}
