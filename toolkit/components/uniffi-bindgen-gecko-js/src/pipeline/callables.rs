/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use super::*;
use crate::ConcurrencyMode;

use anyhow::{anyhow, bail, Result};

pub fn pass(module: &mut Module) -> Result<()> {
    // We have to generate for_callback_interface for now.  In the future, this probably should be
    // done in the general pipeline.
    module.visit_mut(|cbi: &mut CallbackInterface| {
        cbi.visit_mut(|callable: &mut Callable| {
            if let CallableKind::VTableMethod {
                for_callback_interface,
                ..
            } = &mut callable.kind
            {
                *for_callback_interface = true;
            }
        });
    });

    let async_wrappers = &module.config.async_wrappers;
    let module_name = &module.name;

    // Track unconfigured callables for later reporting
    let mut unconfigured_callables = Vec::new();

    // Configure all callables
    module.functions.try_visit_mut(|callable: &mut Callable| {
        handle_callable(
            callable,
            async_wrappers,
            &mut unconfigured_callables,
            module_name,
        )
    })?;
    module
        .type_definitions
        .try_visit_mut(|callable: &mut Callable| {
            handle_callable(
                callable,
                async_wrappers,
                &mut unconfigured_callables,
                module_name,
            )
        })?;

    // Report unconfigured callables
    if !unconfigured_callables.is_empty() {
        let mut message = format!(
            "Found {} callables in module '{}' without explicit async/sync configuration in config.toml:\n",
            unconfigured_callables.len(),
            module_name
        );

        for (spec, info, _) in &unconfigured_callables {
            message.push_str(&format!("  - {}: {}\n", spec, info));
        }

        message.push_str(
            "\nPlease add these callables to the `toolkit/components/uniffi-bindgen-gecko-js/config.toml` file with explicit configuration:\n",
        );
        message.push_str(&format!("[{}.async_wrappers]\n", module.crate_name));

        for (spec, _, example) in &unconfigured_callables {
            message.push_str(&format!("\"{spec}\" = {example}\n"));
        }

        // Fail the build with a helpful error message
        return Err(anyhow!(message));
    }

    Ok(())
}

fn handle_callable(
    callable: &mut Callable,
    async_wrappers: &indexmap::IndexMap<String, ConcurrencyMode>,
    unconfigured_callables: &mut Vec<(String, String, String)>,
    module_name: &str,
) -> Result<()> {
    let name = &callable.name;
    let spec = match &callable.kind {
        CallableKind::VTableMethod {
            for_callback_interface: true,
            ..
        } => {
            // Callback interfaces aren't configured using `config.toml` file yet.
            return Ok(());
        }
        CallableKind::Function => name.clone(),
        CallableKind::Method { interface_name, .. }
        | CallableKind::Constructor { interface_name, .. } => {
            format!("{interface_name}.{name}")
        }
        CallableKind::VTableMethod { trait_name, .. } => {
            format!("{trait_name}.{name}")
        }
    };

    let config = async_wrappers.get(&spec);
    // If the config is not set, check for a parent config
    let config = config.or_else(|| match &callable.kind {
        CallableKind::Method {
            interface_name: parent,
            ..
        }
        | CallableKind::Constructor {
            interface_name: parent,
            ..
        }
        | CallableKind::VTableMethod {
            trait_name: parent, ..
        } => async_wrappers.get(parent),
        _ => None,
    });
    // Finally, default to `Async` for async methods
    let config = config.or_else(|| {
        if callable.async_data.is_some() {
            Some(&ConcurrencyMode::Async)
        } else {
            None
        }
    });
    match config {
        Some(ConcurrencyMode::Sync) => {
            callable.is_js_async = false;
            callable.uniffi_scaffolding_method = "UniFFIScaffolding.callSync".to_string();
        }
        Some(ConcurrencyMode::Async) => {
            callable.is_js_async = true;
            callable.uniffi_scaffolding_method = "UniFFIScaffolding.callAsync".to_string();
        }
        Some(ConcurrencyMode::AsyncWrapped) => {
            if matches!(callable.kind, CallableKind::VTableMethod { .. }) {
                bail!(
                    "VTable method '{}' cannot be AsyncWrapped as foreign-implemented trait interfaces don't support async wrapping",
                    spec
                );
            }
            callable.is_js_async = true;
            callable.uniffi_scaffolding_method = "UniFFIScaffolding.callAsyncWrapper".to_string();
        }
        Some(ConcurrencyMode::FireAndForget) => {
            if !matches!(
                callable.kind,
                CallableKind::VTableMethod {
                    for_callback_interface: true,
                    ..
                }
            ) {
                bail!(
                    "VTable method '{}' cannot be FireAndForget as Rust-implemented functions don't support fire-and-forget wrapping",
                    spec
                );
            }
            // no need to set `is_js_async` or `uniffi_scaffolding_method` since these can only
            // be called from Rust.
        }
        None => {
            // Store information about the unconfigured callable
            let source_info = match &callable.kind {
                CallableKind::Function => {
                    format!("Function '{}' in module '{}'", name, module_name)
                }
                CallableKind::Method { interface_name, .. } => format!(
                    "Method '{}.{}' in module '{}'",
                    interface_name, name, module_name
                ),
                CallableKind::Constructor { interface_name, .. } => format!(
                    "Constructor '{}.{}' in module '{}'",
                    interface_name, name, module_name
                ),
                CallableKind::VTableMethod { trait_name, .. } => format!(
                    "VTable method '{}.{}' in module '{}'",
                    trait_name, name, module_name
                ),
            };

            let example = match &callable.kind {
                CallableKind::Function
                | CallableKind::Method { .. }
                | CallableKind::Constructor { .. } => "\"AsyncWrapped\"  # or \"Sync\"".to_string(),
                CallableKind::VTableMethod { .. } => "\"FireAndForget\"  # or \"Sync\"".to_string(),
            };

            unconfigured_callables.push((spec.clone(), source_info, example));

            // Default to async for now - this won't matter if we fail the build
            callable.is_js_async = true;
            callable.uniffi_scaffolding_method = "UniFFIScaffolding.callAsyncWrapper".to_string();
        }
    }

    Ok(())
}
