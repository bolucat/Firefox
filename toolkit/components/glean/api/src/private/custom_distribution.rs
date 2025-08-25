// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use inherent::inherent;
use std::sync::Arc;

use super::{BaseMetricId, ChildMetricMeta, CommonMetricData, MetricId};
use glean::{DistributionData, ErrorType, HistogramType};

use crate::ipc::{need_ipc, with_ipc_payload};
use glean::traits::CustomDistribution;

#[cfg(feature = "with_gecko")]
use super::profiler_utils::{
    truncate_vector_for_marker, DistributionMetricMarker, DistributionValues,
    TelemetryProfilerCategory,
};

/// A custom distribution metric.
///
/// Custom distributions are used to record the distribution of arbitrary values.
#[derive(Clone)]
pub enum CustomDistributionMetric {
    Parent {
        /// The metric's ID. Used for testing and profiler markers. Custom
        /// distribution metrics can be labeled, so we may have either a
        /// metric ID or sub-metric ID.
        id: MetricId,
        inner: Arc<glean::private::CustomDistributionMetric>,
    },
    Child(ChildMetricMeta),
}

define_metric_namer!(CustomDistributionMetric);

impl CustomDistributionMetric {
    /// Create a new custom distribution metric.
    pub fn new(
        id: BaseMetricId,
        meta: CommonMetricData,
        range_min: i64,
        range_max: i64,
        bucket_count: i64,
        histogram_type: HistogramType,
    ) -> Self {
        if need_ipc() {
            CustomDistributionMetric::Child(ChildMetricMeta::from_common_metric_data(id, meta))
        } else {
            let inner = glean::private::CustomDistributionMetric::new(
                meta,
                range_min,
                range_max,
                bucket_count,
                histogram_type,
            );
            CustomDistributionMetric::Parent {
                id: id.into(),
                inner: Arc::new(inner),
            }
        }
    }

    #[cfg(test)]
    pub(crate) fn metric_id(&self) -> MetricId {
        match self {
            CustomDistributionMetric::Parent { id, .. } => *id,
            CustomDistributionMetric::Child(meta) => meta.id.into(),
        }
    }

    #[cfg(test)]
    pub(crate) fn child_metric(&self) -> Self {
        match self {
            CustomDistributionMetric::Parent { id, inner } => {
                // SAFETY: We can unwrap here, as this code is only run in the
                // context of a test. If this code is used elsewhere, the
                // `unwrap` should be replaced with proper error handling of
                // the `None` case.
                CustomDistributionMetric::Child(ChildMetricMeta::from_metric_identifier(
                    id.base_metric_id().unwrap(),
                    inner.as_ref(),
                ))
            }
            CustomDistributionMetric::Child(_) => {
                panic!("Can't get a child metric from a child metric")
            }
        }
    }

    pub fn start_buffer(&self) -> LocalCustomDistribution<'_> {
        match self {
            CustomDistributionMetric::Parent { inner, .. } => {
                LocalCustomDistribution::Parent(inner.start_buffer())
            }
            CustomDistributionMetric::Child(_) => {
                // TODO(bug 1920957): Buffering not implemented for child processes yet. We don't
                // want to panic though.
                log::warn!("Can't get a local custom distribution from a child metric. No data will be recorded.");
                LocalCustomDistribution::Child
            }
        }
    }
}

pub enum LocalCustomDistribution<'a> {
    Parent(glean::private::LocalCustomDistribution<'a>),
    Child,
}

impl LocalCustomDistribution<'_> {
    pub fn accumulate(&mut self, sample: u64) {
        match self {
            LocalCustomDistribution::Parent(p) => p.accumulate(sample),
            LocalCustomDistribution::Child => {
                log::debug!("Can't accumulate local custom distribution in a child process.")
            }
        }
    }
}

#[inherent]
impl CustomDistribution for CustomDistributionMetric {
    pub fn accumulate_samples_signed(&self, samples: Vec<i64>) {
        #[cfg(feature = "with_gecko")]
        let marker_samples = truncate_vector_for_marker(&samples);

        #[allow(unused)]
        let id = match self {
            CustomDistributionMetric::Parent { id, inner } => {
                inner.accumulate_samples(samples);
                *id
            }
            CustomDistributionMetric::Child(meta) => {
                with_ipc_payload(move |payload| {
                    if let Some(v) = payload.custom_samples.get_mut(&meta.id) {
                        v.extend(samples);
                    } else {
                        payload.custom_samples.insert(meta.id, samples);
                    }
                });
                MetricId::Id(meta.id)
            }
        };

        #[cfg(feature = "with_gecko")]
        gecko_profiler::lazy_add_marker!(
            "CustomDistribution::accumulate",
            TelemetryProfilerCategory,
            DistributionMetricMarker::<CustomDistributionMetric, i64>::new(
                id,
                None,
                DistributionValues::Samples(marker_samples),
            )
        );
    }

    pub fn accumulate_single_sample_signed(&self, sample: i64) {
        #[allow(unused)]
        let id = match self {
            CustomDistributionMetric::Parent { id, inner } => {
                inner.accumulate_single_sample(sample);
                *id
            }
            CustomDistributionMetric::Child(meta) => {
                with_ipc_payload(move |payload| {
                    if let Some(v) = payload.custom_samples.get_mut(&meta.id) {
                        v.push(sample);
                    } else {
                        payload.custom_samples.insert(meta.id, vec![sample]);
                    }
                });
                MetricId::Id(meta.id)
            }
        };
        #[cfg(feature = "with_gecko")]
        gecko_profiler::lazy_add_marker!(
            "CustomDistribution::accumulate",
            TelemetryProfilerCategory,
            DistributionMetricMarker::<CustomDistributionMetric, i64>::new(
                id,
                None,
                DistributionValues::Sample(sample)
            )
        );
    }

    pub fn test_get_num_recorded_errors(&self, error: ErrorType) -> i32 {
        match self {
            CustomDistributionMetric::Parent { inner, .. } => {
                inner.test_get_num_recorded_errors(error)
            }
            CustomDistributionMetric::Child(meta) => panic!(
                "Cannot get number of recorded errors for {:?} in non-parent process!",
                meta.id
            ),
        }
    }
}

#[inherent]
impl glean::TestGetValue<DistributionData> for CustomDistributionMetric {
    pub fn test_get_value(&self, ping_name: Option<String>) -> Option<DistributionData> {
        match self {
            CustomDistributionMetric::Parent { inner, .. } => inner.test_get_value(ping_name),
            CustomDistributionMetric::Child(meta) => {
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
    fn smoke_test_custom_distribution() {
        let _lock = lock_test();

        let metric = &metrics::test_only_ipc::a_custom_dist;

        metric.accumulate_samples_signed(vec![1, 2, 3]);

        assert!(metric.test_get_value(Some("test-ping".to_string())).is_some());
    }

    #[test]
    fn custom_distribution_child() {
        let _lock = lock_test();

        let parent_metric = &metrics::test_only_ipc::a_custom_dist;
        parent_metric.accumulate_samples_signed(vec![1, 268435458]);

        {
            let child_metric = parent_metric.child_metric();

            // scope for need_ipc RAII
            let _raii = ipc::test_set_need_ipc(true);

            child_metric.accumulate_samples_signed(vec![4, 268435460]);
        }

        let buf = ipc::take_buf().unwrap();
        assert!(buf.len() > 0);
        assert!(ipc::replay_from_buf(&buf).is_ok());

        let data = parent_metric
            .test_get_value(Some("test-ping".to_string()))
            .expect("should have some data");

        assert_eq!(2, data.values[&1], "Low bucket has 2 values");
        assert_eq!(
            2, data.values[&268435456],
            "Next higher bucket has 2 values"
        );
        assert_eq!(
            1 + 4 + 268435458 + 268435460,
            data.sum,
            "Sum of all recorded values"
        );
    }
}
