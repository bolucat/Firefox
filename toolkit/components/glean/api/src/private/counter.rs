// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use glean::traits::Counter;
use inherent::inherent;
use std::sync::Arc;

use super::{BaseMetricId, ChildMetricMeta, CommonMetricData, MetricId};
use crate::ipc::{need_ipc, with_ipc_payload};

/// A counter metric.
///
/// Used to count things.
/// The value can only be incremented, not decremented.
#[derive(Clone)]
pub enum CounterMetric {
    Parent {
        /// The metric's ID. Used for testing and profiler markers. Counter
        /// metrics can be labeled, so we may have either a metric ID or
        /// sub-metric ID.
        id: MetricId,
        inner: Arc<glean::private::CounterMetric>,
    },
    Child(ChildMetricMeta),
}

define_metric_namer!(CounterMetric);

impl CounterMetric {
    /// Create a new counter metric.
    pub fn new(id: BaseMetricId, meta: CommonMetricData) -> Self {
        if need_ipc() {
            CounterMetric::Child(ChildMetricMeta::from_common_metric_data(id, meta))
        } else {
            let inner = Arc::new(glean::private::CounterMetric::new(meta));
            CounterMetric::Parent {
                id: id.into(),
                inner,
            }
        }
    }

    /// Special-purpose ctor for use by codegen.
    /// Only useful if the metric is:
    ///   * not disabled
    ///   * lifetime: ping
    ///   * and is sent in precisely one ping.
    pub fn codegen_new(id: u32, category: &str, name: &str, ping: &str) -> Self {
        if need_ipc() {
            CounterMetric::Child(ChildMetricMeta::from_name_category_pair(
                BaseMetricId(id),
                name,
                category,
            ))
        } else {
            let inner = Arc::new(glean::private::CounterMetric::new(CommonMetricData {
                category: category.into(),
                name: name.into(),
                send_in_pings: vec![ping.into()],
                ..Default::default()
            }));
            CounterMetric::Parent {
                id: BaseMetricId(id).into(),
                inner,
            }
        }
    }

    /// Special-purpose ctor for use by codegen.
    /// Only useful if the metric is:
    ///   * disabled
    ///   * lifetime: ping
    ///   * and is sent in precisely one ping.
    pub fn codegen_disabled_new(id: u32, category: &str, name: &str, ping: &str) -> Self {
        if need_ipc() {
            CounterMetric::Child(ChildMetricMeta::from_name_category_pair(
                BaseMetricId(id),
                name,
                category,
            ))
        } else {
            let inner = Arc::new(glean::private::CounterMetric::new(CommonMetricData {
                category: category.into(),
                name: name.into(),
                send_in_pings: vec![ping.into()],
                disabled: true,
                ..Default::default()
            }));
            CounterMetric::Parent {
                id: BaseMetricId(id).into(),
                inner,
            }
        }
    }

    #[cfg(test)]
    pub(crate) fn metric_id(&self) -> MetricId {
        match self {
            CounterMetric::Parent { id, .. } => *id,
            CounterMetric::Child(meta) => meta.id.into(),
        }
    }

    #[cfg(test)]
    pub(crate) fn child_metric(&self) -> Self {
        match self {
            CounterMetric::Parent { id, inner } => {
                // SAFETY: We can unwrap here, as this code is only run in the
                // context of a test. If this code is used elsewhere, the
                // `unwrap` should be replaced with proper error handling of
                // the `None` case.
                CounterMetric::Child(ChildMetricMeta::from_metric_identifier(
                    id.base_metric_id().unwrap(),
                    inner.as_ref(),
                ))
            }
            CounterMetric::Child(_) => panic!("Can't get a child metric from a child metric"),
        }
    }
}

#[inherent]
impl Counter for CounterMetric {
    /// Increase the counter by `amount`.
    ///
    /// ## Arguments
    ///
    /// * `amount` - The amount to increase by. Should be positive.
    ///
    /// ## Notes
    ///
    /// Logs an error if the `amount` is 0 or negative.
    pub fn add(&self, amount: i32) {
        #[allow(unused)]
        let id = match self {
            CounterMetric::Parent { id, inner, .. } => {
                inner.add(amount);
                *id
            }
            CounterMetric::Child(meta) => {
                with_ipc_payload(move |payload| {
                    if let Some(v) = payload.counters.get_mut(&meta.id) {
                        *v += amount;
                    } else {
                        payload.counters.insert(meta.id, amount);
                    }
                });
                MetricId::Id(meta.id)
            }
        };

        #[cfg(feature = "with_gecko")]
        if gecko_profiler::can_accept_markers() {
            gecko_profiler::add_marker(
                "Counter::add",
                super::profiler_utils::TelemetryProfilerCategory,
                Default::default(),
                super::profiler_utils::IntLikeMetricMarker::<CounterMetric, i32>::new(
                    id, None, amount,
                ),
            );
        }
    }

    /// **Test-only API.**
    ///
    /// Gets the number of recorded errors for the given metric and error type.
    ///
    /// # Arguments
    ///
    /// * `error` - The type of error
    /// * `ping_name` - represents the optional name of the ping to retrieve the
    ///   metric for. Defaults to the first value in `send_in_pings`.
    ///
    /// # Returns
    ///
    /// The number of errors reported.
    pub fn test_get_num_recorded_errors(&self, error: glean::ErrorType) -> i32 {
        match self {
            CounterMetric::Parent { inner, .. } => inner.test_get_num_recorded_errors(error),
            CounterMetric::Child(meta) => panic!(
                "Cannot get the number of recorded errors for {:?} in non-parent process!",
                meta.id
            ),
        }
    }
}

#[inherent]
impl glean::TestGetValue<i32> for CounterMetric {
    /// **Test-only API.**
    ///
    /// Get the currently stored value as an integer.
    /// This doesn't clear the stored value.
    ///
    /// ## Arguments
    ///
    /// * `ping_name` - the storage name to look into.
    ///
    /// ## Return value
    ///
    /// Returns the stored value or `None` if nothing stored.
    pub fn test_get_value(&self, ping_name: Option<String>) -> Option<i32> {
        match self {
            CounterMetric::Parent { inner, .. } => inner.test_get_value(ping_name),
            CounterMetric::Child(meta) => {
                panic!(
                    "Cannot get test value for {:?} in non-parent process!",
                    meta.id
                )
            }
        }
    }
}

#[cfg(test)]
mod test {
    use crate::{common_test::*, ipc, metrics};

    #[test]
    fn sets_counter_value_parent() {
        let _lock = lock_test();

        let metric = &metrics::test_only_ipc::a_counter;
        metric.add(1);

        assert_eq!(1, metric.test_get_value(Some("test-ping".to_string())).unwrap());
    }

    #[test]
    fn sets_counter_value_child() {
        let _lock = lock_test();

        let parent_metric = &metrics::test_only_ipc::a_counter;
        parent_metric.add(3);

        {
            let child_metric = parent_metric.child_metric();

            // scope for need_ipc RAII
            let _raii = ipc::test_set_need_ipc(true);
            let metric_id = child_metric
                .metric_id()
                .base_metric_id()
                .expect("Cannot perform IPC calls without a BaseMetricId");

            child_metric.add(42);

            ipc::with_ipc_payload(move |payload| {
                assert!(
                    42 == *payload.counters.get(&metric_id).unwrap(),
                    "Stored the correct value in the ipc payload"
                );
            });
        }

        assert!(
            false == ipc::need_ipc(),
            "RAII dropped, should not need ipc any more"
        );
        assert!(ipc::replay_from_buf(&ipc::take_buf().unwrap()).is_ok());

        assert!(
            45 == parent_metric.test_get_value(Some("test-ping".to_string())).unwrap(),
            "Values from the 'processes' should be summed"
        );
    }
}
