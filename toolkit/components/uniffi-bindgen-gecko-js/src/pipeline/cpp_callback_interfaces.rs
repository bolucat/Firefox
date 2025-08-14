/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::{HashMap, HashSet};

use anyhow::{anyhow, bail};
use heck::{ToSnakeCase, ToUpperCamelCase};

use super::*;

pub fn pass(root: &mut Root) -> Result<()> {
    root.cpp_scaffolding.callback_interfaces =
        CombinedItems::try_new(root, |module, ids, items| {
            let module_name = module.name.clone();
            let ffi_func_map: HashMap<String, FfiFunctionType> = module
                .ffi_definitions
                .iter()
                .filter_map(|def| match def {
                    FfiDefinition::FunctionType(func_type) => Some(func_type),
                    _ => None,
                })
                .map(|func_type| (func_type.name.0.clone(), func_type.clone()))
                .collect();

            module.try_visit_mut(|cbi: &mut CallbackInterface| {
                cbi.vtable.callback_interface_id = ids.new_id();
                items.push(generate_cpp_callback_interface(
                    &module_name,
                    &cbi.vtable,
                    &ffi_func_map,
                )?);
                Ok(())
            })?;

            module.try_visit_mut(|int: &mut Interface| {
                if let Some(vtable) = &mut int.vtable {
                    vtable.callback_interface_id = ids.new_id();
                    items.push(generate_cpp_callback_interface(
                        &module_name,
                        vtable,
                        &ffi_func_map,
                    )?);
                }
                Ok(())
            })?;
            Ok(())
        })?;

    let mut seen_async_complete_handlers = HashSet::new();
    root.cpp_scaffolding.async_callback_method_handler_bases = root
        .cpp_scaffolding
        .callback_interfaces
        .try_map(|callback_interfaces| {
            let mut async_callback_method_handler_bases = vec![];
            callback_interfaces.try_visit(|meth: &CppCallbackInterfaceMethod| {
                let CallbackMethodKind::Async(async_data) = &meth.kind else {
                    return Ok(());
                };
                if seen_async_complete_handlers
                    .insert(async_data.callback_handler_base_class.clone())
                {
                    async_callback_method_handler_bases.push(AsyncCallbackMethodHandlerBase {
                        class_name: async_data.callback_handler_base_class.clone(),
                        complete_callback_type_name: async_data.complete_callback_type_name.clone(),
                        result_type_name: async_data.result_type_name.clone(),
                        return_type: meth.return_ty.clone(),
                    });
                };
                Ok(())
            })?;
            Ok(async_callback_method_handler_bases)
        })?;
    Ok(())
}

fn map_method(
    meth: &VTableMethod,
    ffi_func: FfiFunctionType,
    module_name: &str,
    interface_name: &str,
) -> Result<CppCallbackInterfaceMethod> {
    let (return_ty, out_pointer_ty) = match &meth.callable.return_type.ty {
        Some(ty) => (
            Some(FfiValueReturnType {
                ty: ty.ffi_type.clone(),
                ..FfiValueReturnType::default()
            }),
            FfiType::MutReference(Box::new(ty.ffi_type.ty.clone())),
        ),
        None => (None, FfiType::VoidPointer),
    };
    let kind = match meth.callable.async_data.as_ref() {
        // Callback interface methods defined as sync in the Rust code always `FireAndForget` (for now)
        None => CallbackMethodKind::FireAndForget,
        // Callback interface methods defined as async in the Rust code are always `Async`
        Some(async_data) => CallbackMethodKind::Async(CppCallbackInterfaceMethodAsyncData {
            callback_handler_base_class: async_callback_method_handler_base_class(&meth.callable)?,
            complete_callback_type_name: async_data.ffi_foreign_future_complete.0.clone(),
            result_type_name: async_data.ffi_foreign_future_result.0.clone(),
        }),
    };

    Ok(CppCallbackInterfaceMethod {
        kind,
        arguments: meth
            .callable
            .arguments
            .iter()
            .map(|a| FfiValueArgument {
                name: a.name.clone(),
                ty: a.ty.ffi_type.clone(),
                ..FfiValueArgument::default()
            })
            .collect(),
        fn_name: format!(
            "callback_interface_{}_{}_{}",
            module_name.to_snake_case(),
            interface_name.to_snake_case(),
            meth.callable.name.to_snake_case(),
        ),
        base_class_name: match &meth.callable.async_data {
            // Note: non-async callbacks are currently always treated as fire-and-forget methods.
            // These fire-and-forget methods inherit from `AsyncCallbackMethodHandlerBase` directly
            // and have a no-op `HandleReturn` method.
            None => "AsyncCallbackMethodHandlerBase".to_string(),
            Some(_) => async_callback_method_handler_base_class(&meth.callable)?,
        },
        handler_class_name: format!(
            "CallbackInterfaceMethod{}{}{}",
            module_name.to_upper_camel_case(),
            interface_name.to_upper_camel_case(),
            meth.callable.name.to_upper_camel_case(),
        ),
        return_ty,
        out_pointer_ty: FfiTypeNode {
            ty: out_pointer_ty,
            ..FfiTypeNode::default()
        },
        ffi_func,
    })
}

fn async_callback_method_handler_base_class(callable: &Callable) -> Result<String> {
    let return_type = callable.return_type.ty.as_ref().map(|ty| &ty.ffi_type.ty);
    Ok(match return_type {
        None => "AsyncCallbackMethodHandlerBaseVoid".to_string(),
        Some(return_type) => match return_type {
            FfiType::RustArcPtr {
                module_name,
                object_name,
            } => {
                format!(
                    "AsyncCallbackMethodHandlerBaseRustArcPtr{}_{}",
                    module_name.to_upper_camel_case(),
                    object_name.to_upper_camel_case()
                )
            }
            FfiType::UInt8 => "AsyncCallbackMethodHandlerBaseUInt8".to_string(),
            FfiType::Int8 => "AsyncCallbackMethodHandlerBaseInt8".to_string(),
            FfiType::UInt16 => "AsyncCallbackMethodHandlerBaseUInt16".to_string(),
            FfiType::Int16 => "AsyncCallbackMethodHandlerBaseInt16".to_string(),
            FfiType::UInt32 => "AsyncCallbackMethodHandlerBaseUInt32".to_string(),
            FfiType::Int32 => "AsyncCallbackMethodHandlerBaseInt32".to_string(),
            FfiType::UInt64 => "AsyncCallbackMethodHandlerBaseUInt64".to_string(),
            FfiType::Int64 => "AsyncCallbackMethodHandlerBaseInt64".to_string(),
            FfiType::Float32 => "AsyncCallbackMethodHandlerBaseFloat32".to_string(),
            FfiType::Float64 => "AsyncCallbackMethodHandlerBaseFloat64".to_string(),
            FfiType::RustBuffer(_) => "AsyncCallbackMethodHandlerBaseRustBuffer".to_string(),
            ty => bail!("Async return type not supported: {ty:?}"),
        },
    })
}

fn generate_cpp_callback_interface(
    module_name: &str,
    vtable: &VTable,
    ffi_func_map: &HashMap<String, FfiFunctionType>,
) -> Result<CppCallbackInterface> {
    let interface_name = &vtable.interface_name;
    Ok(CppCallbackInterface {
        id: vtable.callback_interface_id,
        name: interface_name.to_string(),
        // Only generate FFI value class for callback interfaces.  For trait
        // interfaces, we're going to generate one `PointerTypes.cpp` instead.
        ffi_value_class: vtable
            .callback_interface
            .then(|| format!("FfiValueCallbackInterface{module_name}_{interface_name}")),
        handler_var: format!(
            "gUniffiCallbackHandler{}{}",
            module_name.to_upper_camel_case(),
            interface_name.to_upper_camel_case()
        ),
        vtable_var: format!(
            "kUniffiVtable{}{}",
            module_name.to_upper_camel_case(),
            interface_name.to_upper_camel_case()
        ),
        vtable_struct_type: vtable.struct_type.clone(),
        init_fn: vtable.init_fn.clone(),
        free_fn: format!(
            "callback_free_{}_{}",
            module_name.to_snake_case(),
            interface_name.to_snake_case()
        ),
        methods: vtable
            .methods
            .iter()
            .map(|vtable_meth| {
                let FfiType::Function(FfiFunctionTypeName(name)) = &vtable_meth.ffi_type.ty else {
                    bail!(
                        "Invalid FFI TYPE for VTable method {:?}",
                        vtable_meth.ffi_type.ty
                    );
                };
                let ffi_func = ffi_func_map
                    .get(name)
                    .cloned()
                    .ok_or_else(|| anyhow!("Callback interface method not found: {name}"))?;
                map_method(vtable_meth, ffi_func, module_name, interface_name)
            })
            .collect::<Result<Vec<_>>>()?,
    })
}
