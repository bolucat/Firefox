/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the computed value of
//! [`position`][position] values.
//!
//! [position]: https://drafts.csswg.org/css-backgrounds-3/#position

use crate::values::computed::{
    Context, Integer, LengthPercentage, NonNegativeNumber, Percentage, ToComputedValue,
};
use crate::values::generics::position::Position as GenericPosition;
use crate::values::generics::position::PositionComponent as GenericPositionComponent;
use crate::values::generics::position::PositionOrAuto as GenericPositionOrAuto;
use crate::values::generics::position::ZIndex as GenericZIndex;
use crate::values::generics::position::{
    AnchorSideKeyword, GenericAnchorFunction, GenericAnchorSide,
};
use crate::values::generics::position::{AspectRatio as GenericAspectRatio, GenericInset};
pub use crate::values::specified::position::{
    AnchorName, AnchorScope, DashedIdentAndOrTryTactic, PositionAnchor, PositionArea,
    PositionAreaKeyword, PositionAreaType, PositionTryFallbacks, PositionTryOrder,
    PositionVisibility,
};
pub use crate::values::specified::position::{GridAutoFlow, GridTemplateAreas, MasonryAutoFlow};
use crate::Zero;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ToCss};

/// The computed value of a CSS `<position>`
pub type Position = GenericPosition<HorizontalPosition, VerticalPosition>;

/// The computed value of an `auto | <position>`
pub type PositionOrAuto = GenericPositionOrAuto<Position>;

/// The computed value of a CSS horizontal position.
pub type HorizontalPosition = LengthPercentage;

/// The computed value of a CSS vertical position.
pub type VerticalPosition = LengthPercentage;

/// The computed value of anchor side.
pub type AnchorSide = GenericAnchorSide<Percentage>;

impl AnchorSide {
    /// Break down given anchor side into its equivalent keyword and percentage.
    pub fn keyword_and_percentage(&self) -> (AnchorSideKeyword, Percentage) {
        match self {
            Self::Percentage(p) => (AnchorSideKeyword::Start, *p),
            Self::Keyword(k) => {
                if matches!(k, AnchorSideKeyword::Center) {
                    (AnchorSideKeyword::Start, Percentage(0.5))
                } else {
                    (*k, Percentage::zero())
                }
            },
        }
    }
}

/// The computed value of an `anchor()` function.
pub type AnchorFunction = GenericAnchorFunction<Percentage, Inset>;

#[cfg(feature = "gecko")]
use crate::{
    gecko_bindings::structs::AnchorPosOffsetResolutionParams,
    logical_geometry::PhysicalSide,
    values::{computed::Length, DashedIdent},
};

impl AnchorFunction {
    /// Resolve the anchor function with the given resolver. Returns `Err()` if no anchor is found.
    #[cfg(feature = "gecko")]
    pub fn resolve(
        anchor_name: &DashedIdent,
        anchor_side: &AnchorSide,
        prop_side: PhysicalSide,
        params: &AnchorPosOffsetResolutionParams,
    ) -> Result<Length, ()> {
        use crate::gecko_bindings::structs::Gecko_GetAnchorPosOffset;

        let (keyword, percentage) = anchor_side.keyword_and_percentage();
        let mut offset = Length::zero();
        let valid = unsafe {
            Gecko_GetAnchorPosOffset(
                params,
                anchor_name.0.as_ptr(),
                prop_side as u8,
                keyword as u8,
                percentage.0,
                &mut offset,
            )
        };

        if !valid {
            return Err(());
        }

        Ok(offset)
    }
}

/// A computed type for `inset` properties.
pub type Inset = GenericInset<Percentage, LengthPercentage>;

impl Position {
    /// `50% 50%`
    #[inline]
    pub fn center() -> Self {
        Self::new(
            LengthPercentage::new_percent(Percentage(0.5)),
            LengthPercentage::new_percent(Percentage(0.5)),
        )
    }

    /// `0% 0%`
    #[inline]
    pub fn zero() -> Self {
        Self::new(LengthPercentage::zero(), LengthPercentage::zero())
    }
}

impl ToCss for Position {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.horizontal.to_css(dest)?;
        dest.write_char(' ')?;
        self.vertical.to_css(dest)
    }
}

impl GenericPositionComponent for LengthPercentage {
    fn is_center(&self) -> bool {
        match self.to_percentage() {
            Some(Percentage(per)) => per == 0.5,
            _ => false,
        }
    }
}

#[inline]
fn block_or_inline_to_inferred(keyword: PositionAreaKeyword) -> PositionAreaKeyword {
    match keyword {
        PositionAreaKeyword::BlockStart | PositionAreaKeyword::InlineStart => {
            PositionAreaKeyword::Start
        },
        PositionAreaKeyword::BlockEnd | PositionAreaKeyword::InlineEnd => PositionAreaKeyword::End,
        PositionAreaKeyword::SpanBlockStart | PositionAreaKeyword::SpanInlineStart => {
            PositionAreaKeyword::SpanStart
        },
        PositionAreaKeyword::SpanBlockEnd | PositionAreaKeyword::SpanInlineEnd => {
            PositionAreaKeyword::SpanEnd
        },
        PositionAreaKeyword::SelfBlockStart | PositionAreaKeyword::SelfInlineStart => {
            PositionAreaKeyword::SelfStart
        },
        PositionAreaKeyword::SelfBlockEnd | PositionAreaKeyword::SelfInlineEnd => {
            PositionAreaKeyword::SelfEnd
        },
        PositionAreaKeyword::SpanSelfBlockStart | PositionAreaKeyword::SpanSelfInlineStart => {
            PositionAreaKeyword::SpanSelfStart
        },
        PositionAreaKeyword::SpanSelfBlockEnd | PositionAreaKeyword::SpanSelfInlineEnd => {
            PositionAreaKeyword::SpanSelfEnd
        },
        other => other,
    }
}

#[inline]
fn inferred_to_block(keyword: PositionAreaKeyword) -> PositionAreaKeyword {
    match keyword {
        PositionAreaKeyword::Start => PositionAreaKeyword::BlockStart,
        PositionAreaKeyword::End => PositionAreaKeyword::BlockEnd,
        PositionAreaKeyword::SpanStart => PositionAreaKeyword::SpanBlockStart,
        PositionAreaKeyword::SpanEnd => PositionAreaKeyword::SpanBlockEnd,
        PositionAreaKeyword::SelfStart => PositionAreaKeyword::SelfBlockStart,
        PositionAreaKeyword::SelfEnd => PositionAreaKeyword::SelfBlockEnd,
        PositionAreaKeyword::SpanSelfStart => PositionAreaKeyword::SpanSelfBlockStart,
        PositionAreaKeyword::SpanSelfEnd => PositionAreaKeyword::SpanSelfBlockEnd,
        other => other,
    }
}

#[inline]
fn inferred_to_inline(keyword: PositionAreaKeyword) -> PositionAreaKeyword {
    match keyword {
        PositionAreaKeyword::Start => PositionAreaKeyword::InlineStart,
        PositionAreaKeyword::End => PositionAreaKeyword::InlineEnd,
        PositionAreaKeyword::SpanStart => PositionAreaKeyword::SpanInlineStart,
        PositionAreaKeyword::SpanEnd => PositionAreaKeyword::SpanInlineEnd,
        PositionAreaKeyword::SelfStart => PositionAreaKeyword::SelfInlineStart,
        PositionAreaKeyword::SelfEnd => PositionAreaKeyword::SelfInlineEnd,
        PositionAreaKeyword::SpanSelfStart => PositionAreaKeyword::SpanSelfInlineStart,
        PositionAreaKeyword::SpanSelfEnd => PositionAreaKeyword::SpanSelfInlineEnd,
        other => other,
    }
}

// This exists because the spec currently says that further simplifications
// should be made to the computed value. That's confusing though, and probably
// all these simplifications should be wrapped up into the simplifications that
// we make to the specified value. I.e. all this should happen in
// PositionArea::parse_internal().
// See also https://github.com/w3c/csswg-drafts/issues/12759
impl ToComputedValue for PositionArea {
    type ComputedValue = PositionArea;

    fn to_computed_value(&self, _context: &Context) -> Self::ComputedValue {
        let mut computed = self.clone();

        let pair_type = self.get_type();

        if pair_type == PositionAreaType::Logical || pair_type == PositionAreaType::SelfLogical {
            if computed.second != PositionAreaKeyword::None {
                computed.first = block_or_inline_to_inferred(computed.first);
                computed.second = block_or_inline_to_inferred(computed.second);
            }
        } else if pair_type == PositionAreaType::Inferred
            || pair_type == PositionAreaType::SelfInferred
        {
            if computed.second == PositionAreaKeyword::SpanAll {
                // We remove the superfluous span-all, converting the inferred
                // keyword to a logical keyword to avoid ambiguity, per spec.
                computed.first = inferred_to_block(computed.first);
                computed.second = PositionAreaKeyword::None;
            } else if computed.first == PositionAreaKeyword::SpanAll {
                computed.first = computed.second;
                computed.first = inferred_to_inline(computed.first);
                computed.second = PositionAreaKeyword::None;
            }
        }

        if computed.first == computed.second {
            computed.second = PositionAreaKeyword::None;
        }

        computed
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        computed.clone()
    }
}

/// A computed value for the `z-index` property.
pub type ZIndex = GenericZIndex<Integer>;

/// A computed value for the `aspect-ratio` property.
pub type AspectRatio = GenericAspectRatio<NonNegativeNumber>;
