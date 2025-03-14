/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A query condition:
//!
//! https://drafts.csswg.org/mediaqueries-4/#typedef-media-condition
//! https://drafts.csswg.org/css-contain-3/#typedef-container-condition

use super::{FeatureFlags, FeatureType, QueryFeatureExpression};
use crate::custom_properties;
use crate::values::{computed, AtomString};
use crate::{error_reporting::ContextualParseError, parser::ParserContext};
use cssparser::{Parser, SourcePosition, Token};
use selectors::kleene_value::KleeneValue;
use servo_arc::Arc;
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

/// A binary `and` or `or` operator.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, Parse, PartialEq, ToCss, ToShmem)]
#[allow(missing_docs)]
pub enum Operator {
    And,
    Or,
}

/// Whether to allow an `or` condition or not during parsing.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, ToCss)]
enum AllowOr {
    Yes,
    No,
}

/// A style query feature:
/// https://drafts.csswg.org/css-conditional-5/#typedef-style-feature
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct StyleFeature {
    name: custom_properties::Name,
    // TODO: This is a "primary" reference, probably should be unconditionally measured.
    #[ignore_malloc_size_of = "Arc"]
    value: Option<Arc<custom_properties::SpecifiedValue>>,
}

impl ToCss for StyleFeature {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        dest.write_str("--")?;
        crate::values::serialize_atom_identifier(&self.name, dest)?;
        if let Some(ref v) = self.value {
            dest.write_str(": ")?;
            v.to_css(dest)?;
        }
        Ok(())
    }
}

impl StyleFeature {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        if !static_prefs::pref!("layout.css.style-queries.enabled") ||
            feature_type != FeatureType::Container
        {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        // TODO: Allow parsing nested style feature queries.
        let ident = input.expect_ident()?;
        // TODO(emilio): Maybe support non-custom properties?
        let name = match custom_properties::parse_name(ident.as_ref()) {
            Ok(name) => custom_properties::Name::from(name),
            Err(()) => return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
        };
        let value = if input.try_parse(|i| i.expect_colon()).is_ok() {
            input.skip_whitespace();
            Some(Arc::new(custom_properties::SpecifiedValue::parse(
                input,
                &context.url_data,
            )?))
        } else {
            None
        };
        Ok(Self { name, value })
    }

    fn matches(&self, ctx: &computed::Context) -> KleeneValue {
        // FIXME(emilio): Confirm this is the right style to query.
        let registration = ctx
            .builder
            .stylist
            .expect("container queries should have a stylist around")
            .get_custom_property_registration(&self.name);
        let current_value = ctx
            .inherited_custom_properties()
            .get(registration, &self.name);
        KleeneValue::from(match self.value {
            Some(ref v) => current_value.is_some_and(|cur| {
                custom_properties::compute_variable_value(v, registration, ctx)
                    .is_some_and(|v| v == *cur)
            }),
            None => current_value.is_some(),
        })
    }
}

/// A boolean value for a pref query.
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    Eq,
    Parse,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToShmem,
)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum BoolValue {
    False,
    True,
}

/// Simple values we support for -moz-pref(). We don't want to deal with calc() and other
/// shenanigans for now.
#[derive(
    Clone,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToShmem,
)]
#[repr(u8)]
pub enum MozPrefFeatureValue<I> {
    /// No pref value, implicitly bool, but also used to represent missing prefs.
    #[css(skip)]
    None,
    /// A bool value.
    Boolean(BoolValue),
    /// An integer value, useful for int prefs.
    Integer(I),
    /// A string pref value.
    String(crate::values::AtomString),
}

type SpecifiedMozPrefFeatureValue = MozPrefFeatureValue<crate::values::specified::Integer>;
/// The computed -moz-pref() value.
pub type ComputedMozPrefFeatureValue = MozPrefFeatureValue<crate::values::computed::Integer>;

/// A custom -moz-pref(<name>, <value>) query feature.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct MozPrefFeature {
    name: crate::values::AtomString,
    value: SpecifiedMozPrefFeatureValue,
}

impl MozPrefFeature {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        use crate::parser::Parse;
        if !context.chrome_rules_enabled() || feature_type != FeatureType::Media {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        let name = AtomString::parse(context, input)?;
        let value = if input.try_parse(|i| i.expect_comma()).is_ok() {
            SpecifiedMozPrefFeatureValue::parse(context, input)?
        } else {
            SpecifiedMozPrefFeatureValue::None
        };
        Ok(Self { name, value })
    }

    fn matches(&self, ctx: &computed::Context) -> KleeneValue {
        use crate::values::computed::ToComputedValue;
        let value = self.value.to_computed_value(ctx);
        KleeneValue::from(unsafe {
            crate::gecko_bindings::bindings::Gecko_EvalMozPrefFeature(self.name.as_ptr(), &value)
        })
    }
}

impl ToCss for MozPrefFeature {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        self.name.to_css(dest)?;
        if !matches!(self.value, MozPrefFeatureValue::None) {
            dest.write_str(", ")?;
            self.value.to_css(dest)?;
        }
        Ok(())
    }
}

/// Represents a condition.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub enum QueryCondition {
    /// A simple feature expression, implicitly parenthesized.
    Feature(QueryFeatureExpression),
    /// A negation of a condition.
    Not(Box<QueryCondition>),
    /// A set of joint operations.
    Operation(Box<[QueryCondition]>, Operator),
    /// A condition wrapped in parenthesis.
    InParens(Box<QueryCondition>),
    /// A <style> query.
    Style(StyleFeature),
    /// A -moz-pref() query.
    MozPref(MozPrefFeature),
    /// [ <function-token> <any-value>? ) ] | [ ( <any-value>? ) ]
    GeneralEnclosed(String),
}

impl ToCss for QueryCondition {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        match *self {
            // NOTE(emilio): QueryFeatureExpression already includes the
            // parenthesis.
            QueryCondition::Feature(ref f) => f.to_css(dest),
            QueryCondition::Not(ref c) => {
                dest.write_str("not ")?;
                c.to_css(dest)
            },
            QueryCondition::InParens(ref c) => {
                dest.write_char('(')?;
                c.to_css(dest)?;
                dest.write_char(')')
            },
            QueryCondition::Style(ref c) => {
                dest.write_str("style(")?;
                c.to_css(dest)?;
                dest.write_char(')')
            },
            QueryCondition::MozPref(ref c) => {
                dest.write_str("-moz-pref(")?;
                c.to_css(dest)?;
                dest.write_char(')')
            },
            QueryCondition::Operation(ref list, op) => {
                let mut iter = list.iter();
                iter.next().unwrap().to_css(dest)?;
                for item in iter {
                    dest.write_char(' ')?;
                    op.to_css(dest)?;
                    dest.write_char(' ')?;
                    item.to_css(dest)?;
                }
                Ok(())
            },
            QueryCondition::GeneralEnclosed(ref s) => dest.write_str(&s),
        }
    }
}

/// <https://drafts.csswg.org/css-syntax-3/#typedef-any-value>
fn consume_any_value<'i, 't>(input: &mut Parser<'i, 't>) -> Result<(), ParseError<'i>> {
    input.expect_no_error_token().map_err(Into::into)
}

impl QueryCondition {
    /// Parse a single condition.
    pub fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(context, input, feature_type, AllowOr::Yes)
    }

    fn visit<F>(&self, visitor: &mut F)
    where
        F: FnMut(&Self),
    {
        visitor(self);
        match *self {
            Self::Feature(..) | Self::GeneralEnclosed(..) | Self::Style(..) | Self::MozPref(..) => {
            },
            Self::Not(ref cond) => cond.visit(visitor),
            Self::Operation(ref conds, _op) => {
                for cond in conds.iter() {
                    cond.visit(visitor);
                }
            },
            Self::InParens(ref cond) => cond.visit(visitor),
        }
    }

    /// Returns the union of all flags in the expression. This is useful for
    /// container queries.
    pub fn cumulative_flags(&self) -> FeatureFlags {
        let mut result = FeatureFlags::empty();
        self.visit(&mut |condition| {
            if let Self::Style(..) = condition {
                result.insert(FeatureFlags::STYLE);
            }
            if let Self::Feature(ref f) = condition {
                result.insert(f.feature_flags())
            }
        });
        result
    }

    /// Parse a single condition, disallowing `or` expressions.
    ///
    /// To be used from the legacy query syntax.
    pub fn parse_disallow_or<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        Self::parse_internal(context, input, feature_type, AllowOr::No)
    }

    /// https://drafts.csswg.org/mediaqueries-5/#typedef-media-condition or
    /// https://drafts.csswg.org/mediaqueries-5/#typedef-media-condition-without-or
    /// (depending on `allow_or`).
    fn parse_internal<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
        allow_or: AllowOr,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        if input.try_parse(|i| i.expect_ident_matching("not")).is_ok() {
            let inner_condition = Self::parse_in_parens(context, input, feature_type)?;
            return Ok(QueryCondition::Not(Box::new(inner_condition)));
        }

        let first_condition = Self::parse_in_parens(context, input, feature_type)?;
        let operator = match input.try_parse(Operator::parse) {
            Ok(op) => op,
            Err(..) => return Ok(first_condition),
        };

        if allow_or == AllowOr::No && operator == Operator::Or {
            return Err(location.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        let mut conditions = vec![];
        conditions.push(first_condition);
        conditions.push(Self::parse_in_parens(context, input, feature_type)?);

        let delim = match operator {
            Operator::And => "and",
            Operator::Or => "or",
        };

        loop {
            if input.try_parse(|i| i.expect_ident_matching(delim)).is_err() {
                return Ok(QueryCondition::Operation(
                    conditions.into_boxed_slice(),
                    operator,
                ));
            }

            conditions.push(Self::parse_in_parens(context, input, feature_type)?);
        }
    }

    fn parse_in_parenthesis_block<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        // Base case. Make sure to preserve this error as it's more generally
        // relevant.
        let feature_error = match input.try_parse(|input| {
            QueryFeatureExpression::parse_in_parenthesis_block(context, input, feature_type)
        }) {
            Ok(expr) => return Ok(Self::Feature(expr)),
            Err(e) => e,
        };
        if let Ok(inner) = Self::parse(context, input, feature_type) {
            return Ok(Self::InParens(Box::new(inner)));
        }
        Err(feature_error)
    }

    fn try_parse_block<'i, T, F>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
        start: SourcePosition,
        parse: F,
    ) -> Option<T>
    where
        F: for<'tt> FnOnce(&mut Parser<'i, 'tt>) -> Result<T, ParseError<'i>>,
    {
        let nested = input.try_parse(|input| input.parse_nested_block(parse));
        match nested {
            Ok(nested) => Some(nested),
            Err(e) => {
                // We're about to swallow the error in a `<general-enclosed>`
                // condition, so report it while we can.
                let loc = e.location;
                let error = ContextualParseError::InvalidMediaRule(input.slice_from(start), e);
                context.log_css_error(loc, error);
                None
            },
        }
    }

    /// Parse a condition in parentheses, or `<general-enclosed>`.
    ///
    /// https://drafts.csswg.org/mediaqueries/#typedef-media-in-parens
    pub fn parse_in_parens<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        feature_type: FeatureType,
    ) -> Result<Self, ParseError<'i>> {
        input.skip_whitespace();
        let start = input.position();
        let start_location = input.current_source_location();
        match *input.next()? {
            Token::ParenthesisBlock => {
                let nested = Self::try_parse_block(context, input, start, |input| {
                    Self::parse_in_parenthesis_block(context, input, feature_type)
                });
                if let Some(nested) = nested {
                    return Ok(nested);
                }
            },
            Token::Function(ref name) => {
                match_ignore_ascii_case! { name,
                    "style" => {
                        let feature = Self::try_parse_block(context, input, start, |input| {
                            StyleFeature::parse(context, input, feature_type)
                        });
                        if let Some(feature) = feature {
                            return Ok(Self::Style(feature));
                        }
                    },
                    "-moz-pref" => {
                        let feature = Self::try_parse_block(context, input, start, |input| {
                            MozPrefFeature::parse(context, input, feature_type)
                        });
                        if let Some(feature) = feature {
                            return Ok(Self::MozPref(feature));
                        }
                    },
                    _ => {},
                }
            },
            ref t => return Err(start_location.new_unexpected_token_error(t.clone())),
        }
        input.parse_nested_block(consume_any_value)?;
        Ok(Self::GeneralEnclosed(input.slice_from(start).to_owned()))
    }

    /// Whether this condition matches the device and quirks mode.
    /// https://drafts.csswg.org/mediaqueries/#evaluating
    /// https://drafts.csswg.org/mediaqueries/#typedef-general-enclosed
    /// Kleene 3-valued logic is adopted here due to the introduction of
    /// <general-enclosed>.
    pub fn matches(&self, context: &computed::Context) -> KleeneValue {
        match *self {
            QueryCondition::Feature(ref f) => f.matches(context),
            QueryCondition::GeneralEnclosed(_) => KleeneValue::Unknown,
            QueryCondition::InParens(ref c) => c.matches(context),
            QueryCondition::Not(ref c) => !c.matches(context),
            QueryCondition::Style(ref c) => c.matches(context),
            QueryCondition::MozPref(ref c) => c.matches(context),
            QueryCondition::Operation(ref conditions, op) => {
                debug_assert!(!conditions.is_empty(), "We never create an empty op");
                match op {
                    Operator::And => {
                        let mut result = KleeneValue::True;
                        for c in conditions.iter() {
                            result &= c.matches(context);
                            if result == KleeneValue::False {
                                break;
                            }
                        }
                        result
                    },
                    Operator::Or => {
                        let mut result = KleeneValue::False;
                        for c in conditions.iter() {
                            result |= c.matches(context);
                            if result == KleeneValue::True {
                                break;
                            }
                        }
                        result
                    },
                }
            },
        }
    }
}
