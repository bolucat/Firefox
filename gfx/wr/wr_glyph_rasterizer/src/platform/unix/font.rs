/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_camel_case_types)]

use api::{ColorU, GlyphDimensions, FontKey, FontRenderMode};
use api::{FontInstancePlatformOptions, FontLCDFilter, FontHinting};
use api::{FontInstanceFlags, FontTemplate, FontVariation, NativeFontHandle};
use freetype::freetype::{FT_BBox, FT_Outline_Translate, FT_Pixel_Mode, FT_Render_Mode};
use freetype::freetype::{FT_Done_Face, FT_Error, FT_Get_Char_Index, FT_Int32};
use freetype::freetype::{FT_Done_FreeType, FT_Library_SetLcdFilter, FT_Pos};
use freetype::freetype::{FT_F26Dot6, FT_Face, FT_Glyph_Format, FT_Long, FT_UInt};
use freetype::freetype::{FT_GlyphSlot, FT_LcdFilter, FT_New_Face, FT_New_Memory_Face};
use freetype::freetype::{FT_Init_FreeType, FT_Load_Glyph, FT_Render_Glyph};
use freetype::freetype::{FT_Library, FT_Outline_Get_CBox, FT_Set_Char_Size, FT_Select_Size};
use freetype::freetype::{FT_Fixed, FT_Matrix, FT_Set_Transform, FT_String, FT_ULong, FT_Vector};
use freetype::freetype::{FT_Err_Unimplemented_Feature, FT_MulFix, FT_Outline_Embolden};
use freetype::freetype::{FT_LOAD_COLOR, FT_LOAD_DEFAULT, FT_LOAD_FORCE_AUTOHINT};
use freetype::freetype::{FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH, FT_LOAD_NO_AUTOHINT};
use freetype::freetype::{FT_LOAD_NO_BITMAP, FT_LOAD_NO_HINTING};
use freetype::freetype::{FT_FACE_FLAG_SCALABLE, FT_FACE_FLAG_FIXED_SIZES};
use freetype::freetype::FT_FACE_FLAG_MULTIPLE_MASTERS;
use freetype::succeeded;
use crate::rasterizer::{FontInstance, GlyphFormat, GlyphKey};
use crate::rasterizer::{GlyphRasterError, GlyphRasterResult, RasterizedGlyph};
use crate::types::FastHashMap;
#[cfg(any(not(target_os = "android"), feature = "dynamic_freetype"))]
use libc::{dlsym, RTLD_DEFAULT};
use libc::free;
use std::{cmp, mem, ptr, slice};
use std::cmp::max;
use std::ffi::CString;
use std::sync::{Arc, Condvar, Mutex, MutexGuard};

// These constants are not present in the freetype
// bindings due to bindgen not handling the way
// the macros are defined.
//const FT_LOAD_TARGET_NORMAL: FT_UInt = 0 << 16;
const FT_LOAD_TARGET_LIGHT: FT_UInt  = 1 << 16;
const FT_LOAD_TARGET_MONO: FT_UInt   = 2 << 16;
const FT_LOAD_TARGET_LCD: FT_UInt    = 3 << 16;
const FT_LOAD_TARGET_LCD_V: FT_UInt  = 4 << 16;

#[repr(C)]
struct FT_Var_Axis {
    pub name: *mut FT_String,
    pub minimum: FT_Fixed,
    pub def: FT_Fixed,
    pub maximum: FT_Fixed,
    pub tag: FT_ULong,
    pub strid: FT_UInt,
}

#[repr(C)]
struct FT_Var_Named_Style {
    pub coords: *mut FT_Fixed,
    pub strid: FT_UInt,
    pub psid: FT_UInt,
}

#[repr(C)]
struct FT_MM_Var {
    pub num_axis: FT_UInt,
    pub num_designs: FT_UInt,
    pub num_namedstyles: FT_UInt,
    pub axis: *mut FT_Var_Axis,
    pub namedstyle: *mut FT_Var_Named_Style,
}

#[inline]
pub fn unimplemented(error: FT_Error) -> bool {
    error == FT_Err_Unimplemented_Feature as FT_Error
}

// Use dlsym to check for symbols. If not available. just return an unimplemented error.
#[cfg(any(not(target_os = "android"), feature = "dynamic_freetype"))]
macro_rules! ft_dyn_fn {
    ($func_name:ident($($arg_name:ident:$arg_type:ty),*) -> FT_Error) => {
        #[allow(non_snake_case)]
        unsafe fn $func_name($($arg_name:$arg_type),*) -> FT_Error {
            extern "C" fn unimpl_func($(_:$arg_type),*) -> FT_Error {
                FT_Err_Unimplemented_Feature as FT_Error
            }
            lazy_static! {
                static ref FUNC: unsafe extern "C" fn($($arg_type),*) -> FT_Error = {
                    unsafe {
                        let cname = CString::new(stringify!($func_name)).unwrap();
                        let ptr = dlsym(RTLD_DEFAULT, cname.as_ptr());
                        if !ptr.is_null() { mem::transmute(ptr) } else { unimpl_func }
                    }
                };
            }
            (*FUNC)($($arg_name),*)
        }
    }
}

// On Android, just statically link in the symbols...
#[cfg(all(target_os = "android", not(feature = "dynamic_freetype")))]
macro_rules! ft_dyn_fn {
    ($($proto:tt)+) => { extern "C" { fn $($proto)+; } }
}

ft_dyn_fn!(FT_Get_MM_Var(face: FT_Face, desc: *mut *mut FT_MM_Var) -> FT_Error);
ft_dyn_fn!(FT_Done_MM_Var(library: FT_Library, desc: *mut FT_MM_Var) -> FT_Error);
ft_dyn_fn!(FT_Set_Var_Design_Coordinates(face: FT_Face, num_vals: FT_UInt, vals: *mut FT_Fixed) -> FT_Error);
ft_dyn_fn!(FT_Get_Var_Design_Coordinates(face: FT_Face, num_vals: FT_UInt, vals: *mut FT_Fixed) -> FT_Error);

extern "C" {
    fn FT_GlyphSlot_Embolden(slot: FT_GlyphSlot);
}

// Custom version of FT_GlyphSlot_Embolden to be less aggressive with outline
// fonts than the default implementation in FreeType.
#[no_mangle]
pub extern "C" fn mozilla_glyphslot_embolden_less(slot: FT_GlyphSlot) {
    if slot.is_null() {
        return;
    }

    let slot_ = unsafe { &mut *slot };
    let format = slot_.format;
    if format != FT_Glyph_Format::FT_GLYPH_FORMAT_OUTLINE {
        // For non-outline glyphs, just fall back to FreeType's function.
        unsafe { FT_GlyphSlot_Embolden(slot) };
        return;
    }

    let face_ = unsafe { *slot_.face };

    // FT_GlyphSlot_Embolden uses a divisor of 24 here; we'll be only half as
    // bold.
    let size_ = unsafe { *face_.size };
    let strength =
        unsafe { FT_MulFix(face_.units_per_EM as FT_Long,
                           size_.metrics.y_scale) / 48 };
    unsafe { FT_Outline_Embolden(&mut slot_.outline, strength) };

    // Adjust metrics to suit the fattened glyph.
    if slot_.advance.x != 0 {
        slot_.advance.x += strength;
    }
    if slot_.advance.y != 0 {
        slot_.advance.y += strength;
    }
    slot_.metrics.width += strength;
    slot_.metrics.height += strength;
    slot_.metrics.horiAdvance += strength;
    slot_.metrics.vertAdvance += strength;
    slot_.metrics.horiBearingY += strength;
}

struct CachedFont {
    template: FontTemplate,
    face: FT_Face,
    mm_var: *mut FT_MM_Var,
    variations: Vec<FontVariation>,
}

impl Drop for CachedFont {
    fn drop(&mut self) {
        unsafe {
            if !self.mm_var.is_null() &&
                unimplemented(FT_Done_MM_Var((*(*self.face).glyph).library, self.mm_var)) {
                free(self.mm_var as _);
            }

            FT_Done_Face(self.face);
        }
    }
}

struct FontCache {
    lib: FT_Library,
    // Maps a template to a cached font that may be used across all threads.
    fonts: FastHashMap<FontTemplate, Arc<Mutex<CachedFont>>>,
    // The current LCD filter installed in the library.
    lcd_filter: FontLCDFilter,
    // The number of threads currently relying on the LCD filter state.
    lcd_filter_uses: usize,
}

// FreeType resources are safe to move between threads as long as they
// are not concurrently accessed. In our case, everything is behind a
// Mutex so it is safe to move them between threads.
unsafe impl Send for CachedFont {}
unsafe impl Send for FontCache {}

impl FontCache {
    fn new() -> Self {
        let mut lib: FT_Library = ptr::null_mut();
        let result = unsafe { FT_Init_FreeType(&mut lib) };
        if succeeded(result) {
            // Ensure the library uses the default LCD filter initially.
            unsafe { FT_Library_SetLcdFilter(lib, FT_LcdFilter::FT_LCD_FILTER_DEFAULT) };
        } else {
            panic!("Failed to initialize FreeType - {}", result)
        }

        FontCache {
            lib,
            fonts: FastHashMap::default(),
            lcd_filter: FontLCDFilter::Default,
            lcd_filter_uses: 0,
        }
    }

    fn add_font(&mut self, template: FontTemplate) -> Result<Arc<Mutex<CachedFont>>, FT_Error> {
        if let Some(cached) = self.fonts.get(&template) {
            return Ok(cached.clone());
        }
        unsafe {
            let mut face: FT_Face = ptr::null_mut();
            let result = match template {
                FontTemplate::Raw(ref bytes, index) => {
                    FT_New_Memory_Face(
                        self.lib,
                        bytes.as_ptr(),
                        bytes.len() as FT_Long,
                        index as FT_Long,
                        &mut face,
                    )
                }
                FontTemplate::Native(NativeFontHandle { ref path, index }) => {
                    let str = path.as_os_str().to_str().unwrap();
                    let cstr = CString::new(str).unwrap();
                    FT_New_Face(
                        self.lib,
                        cstr.as_ptr(),
                        index as FT_Long,
                        &mut face,
                    )
                }
            };
            if !succeeded(result) || face.is_null() {
                return Err(result);
            }
            let mut mm_var = ptr::null_mut();
            if ((*face).face_flags & (FT_FACE_FLAG_MULTIPLE_MASTERS as FT_Long)) != 0 &&
               succeeded(FT_Get_MM_Var(face, &mut mm_var)) {
                // Calling this before FT_Set_Var_Design_Coordinates avoids a bug with font variations
                // not initialized properly in the font face, even if we ignore the result.
                // See bug 1647035.
                let mut tmp = [0; 16];
                let res = FT_Get_Var_Design_Coordinates(
                    face,
                    (*mm_var).num_axis.min(16),
                    tmp.as_mut_ptr()
                );
                debug_assert!(succeeded(res));
            }
            let cached = Arc::new(Mutex::new(CachedFont {
                template: template.clone(),
                face,
                mm_var,
                variations: Vec::new(),
            }));
            self.fonts.insert(template, cached.clone());
            Ok(cached)
        }
    }

    fn delete_font(&mut self, cached: Arc<Mutex<CachedFont>>) {
        self.fonts.remove(&cached.lock().unwrap().template);
    }
}

impl Drop for FontCache {
    fn drop(&mut self) {
        self.fonts.clear();
        unsafe {
            FT_Done_FreeType(self.lib);
        }
    }
}

lazy_static! {
    static ref FONT_CACHE: Mutex<FontCache> = Mutex::new(FontCache::new());
    static ref LCD_FILTER_UNUSED: Condvar = Condvar::new();
}

pub struct FontContext {
    fonts: FastHashMap<FontKey, Arc<Mutex<CachedFont>>>,
}

fn get_skew_bounds(bottom: i32, top: i32, skew_factor: f32, _vertical: bool) -> (f32, f32) {
    let skew_min = (bottom as f32 + 0.5) * skew_factor;
    let skew_max = (top as f32 - 0.5) * skew_factor;
    // Negative skew factor may switch the sense of skew_min and skew_max.
    (skew_min.min(skew_max).floor(), skew_min.max(skew_max).ceil())
}

fn skew_bitmap(
    bitmap: &[u8],
    width: usize,
    height: usize,
    left: i32,
    top: i32,
    skew_factor: f32,
    vertical: bool, // TODO: vertical skew not yet implemented!
) -> (Vec<u8>, usize, i32) {
    let stride = width * 4;
    // Calculate the skewed horizontal offsets of the bottom and top of the glyph.
    let (skew_min, skew_max) = get_skew_bounds(top - height as i32, top, skew_factor, vertical);
    // Allocate enough extra width for the min/max skew offsets.
    let skew_width = width + (skew_max - skew_min) as usize;
    let mut skew_buffer = vec![0u8; skew_width * height * 4];
    for y in 0 .. height {
        // Calculate a skew offset at the vertical center of the current row.
        let offset = (top as f32 - y as f32 - 0.5) * skew_factor - skew_min;
        // Get a blend factor in 0..256 constant across all pixels in the row.
        let blend = (offset.fract() * 256.0) as u32;
        let src_row = y * stride;
        let dest_row = (y * skew_width + offset.floor() as usize) * 4;
        let mut prev_px = [0u32; 4];
        for (src, dest) in
            bitmap[src_row .. src_row + stride].chunks(4).zip(
                skew_buffer[dest_row .. dest_row + stride].chunks_mut(4)
            ) {
            let px = [src[0] as u32, src[1] as u32, src[2] as u32, src[3] as u32];
            // Blend current pixel with previous pixel based on blend factor.
            let next_px = [px[0] * blend, px[1] * blend, px[2] * blend, px[3] * blend];
            dest[0] = ((((px[0] << 8) - next_px[0]) + prev_px[0] + 128) >> 8) as u8;
            dest[1] = ((((px[1] << 8) - next_px[1]) + prev_px[1] + 128) >> 8) as u8;
            dest[2] = ((((px[2] << 8) - next_px[2]) + prev_px[2] + 128) >> 8) as u8;
            dest[3] = ((((px[3] << 8) - next_px[3]) + prev_px[3] + 128) >> 8) as u8;
            // Save the remainder for blending onto the next pixel.
            prev_px = next_px;
        }
        // If the skew misaligns the final pixel, write out the remainder.
        if blend > 0 {
            let dest = &mut skew_buffer[dest_row + stride .. dest_row + stride + 4];
            dest[0] = ((prev_px[0] + 128) >> 8) as u8;
            dest[1] = ((prev_px[1] + 128) >> 8) as u8;
            dest[2] = ((prev_px[2] + 128) >> 8) as u8;
            dest[3] = ((prev_px[3] + 128) >> 8) as u8;
        }
    }
    (skew_buffer, skew_width, left + skew_min as i32)
}

fn transpose_bitmap(bitmap: &[u8], width: usize, height: usize) -> Vec<u8> {
    let mut transposed = vec![0u8; width * height * 4];
    for (y, row) in bitmap.chunks(width * 4).enumerate() {
        let mut offset = y * 4;
        for src in row.chunks(4) {
            transposed[offset .. offset + 4].copy_from_slice(src);
            offset += height * 4;
        }
    }
    transposed
}

fn flip_bitmap_x(bitmap: &mut [u8], width: usize, height: usize) {
    assert!(bitmap.len() == width * height * 4);
    let pixels = unsafe { slice::from_raw_parts_mut(bitmap.as_mut_ptr() as *mut u32, width * height) };
    for row in pixels.chunks_mut(width) {
        row.reverse();
    }
}

fn flip_bitmap_y(bitmap: &mut [u8], width: usize, height: usize) {
    assert!(bitmap.len() == width * height * 4);
    let pixels = unsafe { slice::from_raw_parts_mut(bitmap.as_mut_ptr() as *mut u32, width * height) };
    for y in 0 .. height / 2 {
        let low_row = y * width;
        let high_row = (height - 1 - y) * width;
        for x in 0 .. width {
            pixels.swap(low_row + x, high_row + x);
        }
    }
}

impl FontContext {
    pub fn distribute_across_threads() -> bool {
        false
    }

    pub fn new() -> FontContext {
        FontContext {
            fonts: FastHashMap::default(),
        }
    }

    pub fn add_raw_font(&mut self, font_key: &FontKey, bytes: Arc<Vec<u8>>, index: u32) {
        if !self.fonts.contains_key(font_key) {
            let len = bytes.len();
            match FONT_CACHE.lock().unwrap().add_font(FontTemplate::Raw(bytes, index)) {
                Ok(font) => self.fonts.insert(*font_key, font),
                Err(result) => panic!("adding raw font failed: {} bytes, err={:?}", len, result),
            };
        }
    }

    pub fn add_native_font(&mut self, font_key: &FontKey, native_font_handle: NativeFontHandle) {
        if !self.fonts.contains_key(font_key) {
            let path = native_font_handle.path.to_string_lossy().into_owned();
            match FONT_CACHE.lock().unwrap().add_font(FontTemplate::Native(native_font_handle)) {
                Ok(font) => self.fonts.insert(*font_key, font),
                Err(result) => panic!("adding native font failed: file={} err={:?}", path, result),
            };
        }
    }

    pub fn delete_font(&mut self, font_key: &FontKey) {
        if let Some(cached) = self.fonts.remove(font_key) {
            // If the only references to this font are the FontCache and this FontContext,
            // then delete the font as there are no other existing users.
            if Arc::strong_count(&cached) <= 2 {
                FONT_CACHE.lock().unwrap().delete_font(cached);
            }
        }
    }

    pub fn delete_font_instance(&mut self, _instance: &FontInstance) {
    }

    fn load_glyph(&mut self, font: &FontInstance, glyph: &GlyphKey)
        -> Option<(MutexGuard<CachedFont>, FT_GlyphSlot, f32)> {
        let mut cached = self.fonts.get(&font.font_key)?.lock().ok()?;
        let face = cached.face;

        let mm_var = cached.mm_var;
        if !mm_var.is_null() && font.variations != cached.variations {
            cached.variations.clear();
            cached.variations.extend_from_slice(&font.variations);

            unsafe {
                let num_axis = (*mm_var).num_axis;
                let mut coords: Vec<FT_Fixed> = Vec::with_capacity(num_axis as usize);
                for i in 0 .. num_axis {
                    let axis = (*mm_var).axis.offset(i as isize);
                    let mut value = (*axis).def;
                    for var in &font.variations {
                        if var.tag as FT_ULong == (*axis).tag {
                            value = (var.value * 65536.0 + 0.5) as FT_Fixed;
                            value = cmp::min(value, (*axis).maximum);
                            value = cmp::max(value, (*axis).minimum);
                            break;
                        }
                    }
                    coords.push(value);
                }
                let res = FT_Set_Var_Design_Coordinates(face, num_axis, coords.as_mut_ptr());
                debug_assert!(succeeded(res));
            }
        }

        let mut load_flags = FT_LOAD_DEFAULT;
        let FontInstancePlatformOptions { mut hinting, .. } = font.platform_options.unwrap_or_default();
        // Disable hinting if there is a non-axis-aligned transform.
        if font.synthetic_italics.is_enabled() ||
           ((font.transform.scale_x != 0.0 || font.transform.scale_y != 0.0) &&
            (font.transform.skew_x != 0.0 || font.transform.skew_y != 0.0)) {
            hinting = FontHinting::None;
        }
        match (hinting, font.render_mode) {
            (FontHinting::None, _) => load_flags |= FT_LOAD_NO_HINTING,
            (FontHinting::Mono, _) => load_flags = FT_LOAD_TARGET_MONO,
            (FontHinting::Light, _) => load_flags = FT_LOAD_TARGET_LIGHT,
            (FontHinting::LCD, FontRenderMode::Subpixel) => {
                load_flags = if font.flags.contains(FontInstanceFlags::LCD_VERTICAL) {
                    FT_LOAD_TARGET_LCD_V
                } else {
                    FT_LOAD_TARGET_LCD
                };
                if font.flags.contains(FontInstanceFlags::FORCE_AUTOHINT) {
                    load_flags |= FT_LOAD_FORCE_AUTOHINT;
                }
            }
            _ => {
                if font.flags.contains(FontInstanceFlags::FORCE_AUTOHINT) {
                    load_flags |= FT_LOAD_FORCE_AUTOHINT;
                }
            }
        }

        if font.flags.contains(FontInstanceFlags::NO_AUTOHINT) {
            load_flags |= FT_LOAD_NO_AUTOHINT;
        }
        if !font.flags.contains(FontInstanceFlags::EMBEDDED_BITMAPS) {
            load_flags |= FT_LOAD_NO_BITMAP;
        }

        let face_flags = unsafe { (*face).face_flags };
        if (face_flags & (FT_FACE_FLAG_FIXED_SIZES as FT_Long)) != 0 {
          // We only set FT_LOAD_COLOR if there are bitmap strikes;
          // COLR (color-layer) fonts are handled internally by Gecko, and
          // WebRender is just asked to paint individual layers.
          load_flags |= FT_LOAD_COLOR;
        }

        load_flags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;

        let (x_scale, y_scale) = font.transform.compute_scale().unwrap_or((1.0, 1.0));
        let req_size = font.size.to_f64_px();

        let mut result = if (face_flags & (FT_FACE_FLAG_FIXED_SIZES as FT_Long)) != 0 &&
                            (face_flags & (FT_FACE_FLAG_SCALABLE as FT_Long)) == 0 &&
                            (load_flags & FT_LOAD_NO_BITMAP) == 0 {
            unsafe { FT_Set_Transform(face, ptr::null_mut(), ptr::null_mut()) };
            self.choose_bitmap_size(face, req_size * y_scale)
        } else {
            let mut shape = font.transform.invert_scale(x_scale, y_scale);
            if font.flags.contains(FontInstanceFlags::FLIP_X) {
                shape = shape.flip_x();
            }
            if font.flags.contains(FontInstanceFlags::FLIP_Y) {
                shape = shape.flip_y();
            }
            if font.flags.contains(FontInstanceFlags::TRANSPOSE) {
                shape = shape.swap_xy();
            }
            let (mut tx, mut ty) = (0.0, 0.0);
            if font.synthetic_italics.is_enabled() {
                let (shape_, (tx_, ty_)) = font.synthesize_italics(shape, y_scale * req_size);
                shape = shape_;
                tx = tx_;
                ty = ty_;
            };
            let mut ft_shape = FT_Matrix {
                xx: (shape.scale_x * 65536.0) as FT_Fixed,
                xy: (shape.skew_x * -65536.0) as FT_Fixed,
                yx: (shape.skew_y * -65536.0) as FT_Fixed,
                yy: (shape.scale_y * 65536.0) as FT_Fixed,
            };
            // The delta vector for FT_Set_Transform is in units of 1/64 pixel.
            let mut ft_delta = FT_Vector {
                x: (tx * 64.0) as FT_F26Dot6,
                y: (ty * -64.0) as FT_F26Dot6,
            };
            unsafe {
                FT_Set_Transform(face, &mut ft_shape, &mut ft_delta);
                FT_Set_Char_Size(
                    face,
                    (req_size * x_scale * 64.0 + 0.5) as FT_F26Dot6,
                    (req_size * y_scale * 64.0 + 0.5) as FT_F26Dot6,
                    0,
                    0,
                )
            }
        };

        if !succeeded(result) {
            error!("Unable to set glyph size and transform: {}", result);
            //let raw_error = unsafe { FT_Error_String(result) };
            //if !raw_error.is_ptr() {
            //    error!("\tcode {:?}", CStr::from_ptr(raw_error));
            //}
            debug!(
                "\t[{}] for size {:?} and scale {:?} from font {:?}",
                glyph.index(),
                req_size,
                (x_scale, y_scale),
                font.font_key,
            );
            return None;
        }

        result = unsafe { FT_Load_Glyph(face, glyph.index() as FT_UInt, load_flags as FT_Int32) };
        if !succeeded(result) {
            error!("Unable to load glyph: {}", result);
            //let raw_error = unsafe { FT_Error_String(result) };
            //if !raw_error.is_ptr() {
            //    error!("\tcode {:?}", CStr::from_ptr(raw_error));
            //}
            debug!(
                "\t[{}] with flags {:?} from font {:?}",
                glyph.index(),
                load_flags,
                font.font_key,
            );
            return None;
        }

        let slot = unsafe { (*face).glyph };
        assert!(slot != ptr::null_mut());

        if font.flags.contains(FontInstanceFlags::SYNTHETIC_BOLD) {
            mozilla_glyphslot_embolden_less(slot);
        }

        let format = unsafe { (*slot).format };
        match format {
            FT_Glyph_Format::FT_GLYPH_FORMAT_BITMAP => {
                let bitmap_size = unsafe { (*(*(*slot).face).size).metrics.y_ppem };
                Some((cached, slot, req_size as f32 / bitmap_size as f32))
            }
            FT_Glyph_Format::FT_GLYPH_FORMAT_OUTLINE => Some((cached, slot, 1.0)),
            _ => {
                error!("Unsupported format");
                debug!("format={:?}", format);
                None
            }
        }
    }

    fn pad_bounding_box(font: &FontInstance, cbox: &mut FT_BBox) {
        // Apply extra pixel of padding for subpixel AA, due to the filter.
        if font.render_mode == FontRenderMode::Subpixel {
            // Using an LCD filter may add one full pixel to each side if support is built in.
            // As of FreeType 2.8.1, an LCD filter is always used regardless of settings
            // if support for the patent-encumbered LCD filter algorithms is not built in.
            // Thus, the only reasonable way to guess padding is to unconditonally add it if
            // subpixel AA is used.
            let lcd_extra_pixels = 1;
            let padding = (lcd_extra_pixels * 64) as FT_Pos;
            if font.flags.contains(FontInstanceFlags::LCD_VERTICAL) {
                cbox.yMin -= padding;
                cbox.yMax += padding;
            } else {
                cbox.xMin -= padding;
                cbox.xMax += padding;
            }
        }
    }

    // Get the bounding box for a glyph, accounting for sub-pixel positioning.
    fn get_bounding_box(
        slot: FT_GlyphSlot,
        font: &FontInstance,
        glyph: &GlyphKey,
        scale: f32,
    ) -> FT_BBox {
        // Get the estimated bounding box from FT (control points).
        let mut cbox = FT_BBox { xMin: 0, yMin: 0, xMax: 0, yMax: 0 };

        unsafe {
            FT_Outline_Get_CBox(&(*slot).outline, &mut cbox);
        }

        // For spaces and other non-printable characters, early out.
        if unsafe { (*slot).outline.n_contours } == 0 {
            return cbox;
        }

        Self::pad_bounding_box(font, &mut cbox);

        // Offset the bounding box by subpixel positioning.
        // Convert to 26.6 fixed point format for FT.
        let (dx, dy) = font.get_subpx_offset(glyph);
        let (dx, dy) = (
            (dx / scale as f64 * 64.0 + 0.5) as FT_Pos,
            -(dy / scale as f64 * 64.0 + 0.5) as FT_Pos,
        );
        cbox.xMin += dx;
        cbox.xMax += dx;
        cbox.yMin += dy;
        cbox.yMax += dy;

        // Outset the box to device pixel boundaries
        cbox.xMin &= !63;
        cbox.yMin &= !63;
        cbox.xMax = (cbox.xMax + 63) & !63;
        cbox.yMax = (cbox.yMax + 63) & !63;

        cbox
    }

    fn get_glyph_dimensions_impl(
        slot: FT_GlyphSlot,
        font: &FontInstance,
        glyph: &GlyphKey,
        scale: f32,
        use_transform: bool,
    ) -> Option<GlyphDimensions> {
        let format = unsafe { (*slot).format };
        let (mut left, mut top, mut width, mut height) = match format {
            FT_Glyph_Format::FT_GLYPH_FORMAT_BITMAP => {
                unsafe { (
                    (*slot).bitmap_left as i32,
                    (*slot).bitmap_top as i32,
                    (*slot).bitmap.width as i32,
                    (*slot).bitmap.rows as i32,
                ) }
            }
            FT_Glyph_Format::FT_GLYPH_FORMAT_OUTLINE => {
                let cbox = Self::get_bounding_box(slot, font, glyph, scale);
                (
                    (cbox.xMin >> 6) as i32,
                    (cbox.yMax >> 6) as i32,
                    ((cbox.xMax - cbox.xMin) >> 6) as i32,
                    ((cbox.yMax - cbox.yMin) >> 6) as i32,
                )
            }
            _ => return None,
        };
        let mut advance = unsafe { (*slot).metrics.horiAdvance as f32 / 64.0 };
        if use_transform {
            if scale != 1.0 {
                let x0 = left as f32 * scale;
                let x1 = width as f32 * scale + x0;
                let y1 = top as f32 * scale;
                let y0 = y1 - height as f32 * scale;
                left = x0.round() as i32;
                top = y1.round() as i32;
                width = (x1.ceil() - x0.floor()) as i32;
                height = (y1.ceil() - y0.floor()) as i32;
                advance *= scale;
            }
            // An outline glyph's cbox would have already been transformed inside FT_Load_Glyph,
            // so only handle bitmap glyphs which are not handled by FT_Load_Glyph.
            if format == FT_Glyph_Format::FT_GLYPH_FORMAT_BITMAP {
                if font.synthetic_italics.is_enabled() {
                    let (skew_min, skew_max) = get_skew_bounds(
                        top - height as i32,
                        top,
                        font.synthetic_italics.to_skew(),
                        font.flags.contains(FontInstanceFlags::VERTICAL),
                    );
                    left += skew_min as i32;
                    width += (skew_max - skew_min) as i32;
                }
                if font.flags.contains(FontInstanceFlags::TRANSPOSE) {
                    mem::swap(&mut width, &mut height);
                    mem::swap(&mut left, &mut top);
                    left -= width as i32;
                    top += height as i32;
                }
                if font.flags.contains(FontInstanceFlags::FLIP_X) {
                    left = -(left + width as i32);
                }
                if font.flags.contains(FontInstanceFlags::FLIP_Y) {
                    top = -(top - height as i32);
                }
            }
        }
        Some(GlyphDimensions {
            left,
            top,
            width,
            height,
            advance,
        })
    }

    pub fn get_glyph_index(&mut self, font_key: FontKey, ch: char) -> Option<u32> {
        let cached = self.fonts.get(&font_key)?.lock().ok()?;
        let face = cached.face;
        unsafe {
            let idx = FT_Get_Char_Index(face, ch as _);
            if idx != 0 {
                Some(idx)
            } else {
                None
            }
        }
    }

    pub fn get_glyph_dimensions(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
    ) -> Option<GlyphDimensions> {
        let (_cached, slot, scale) = self.load_glyph(font, key)?;
        Self::get_glyph_dimensions_impl(slot, &font, key, scale, true)
    }

    fn choose_bitmap_size(&self, face: FT_Face, requested_size: f64) -> FT_Error {
        let mut best_dist = unsafe { *(*face).available_sizes.offset(0) }.y_ppem as f64 / 64.0 - requested_size;
        let mut best_size = 0;
        let num_fixed_sizes = unsafe { (*face).num_fixed_sizes };
        for i in 1 .. num_fixed_sizes {
            // Distance is positive if strike is larger than desired size,
            // or negative if smaller. If previously a found smaller strike,
            // then prefer a larger strike. Otherwise, minimize distance.
            let dist = unsafe { *(*face).available_sizes.offset(i as isize) }.y_ppem as f64 / 64.0 - requested_size;
            if (best_dist < 0.0 && dist >= best_dist) || dist.abs() <= best_dist {
                best_dist = dist;
                best_size = i;
            }
        }
        unsafe { FT_Select_Size(face, best_size) }
    }

    pub fn prepare_font(font: &mut FontInstance) {
        match font.render_mode {
            FontRenderMode::Mono => {
                // In mono mode the color of the font is irrelevant.
                font.color = ColorU::new(0xFF, 0xFF, 0xFF, 0xFF);
                // Subpixel positioning is disabled in mono mode.
                font.disable_subpixel_position();
            }
            FontRenderMode::Alpha | FontRenderMode::Subpixel => {
                // We don't do any preblending with FreeType currently, so the color is not used.
                font.color = ColorU::new(0xFF, 0xFF, 0xFF, 0xFF);
            }
        }
    }

    fn rasterize_glyph_outline(
        slot: FT_GlyphSlot,
        font: &FontInstance,
        key: &GlyphKey,
        scale: f32,
    ) -> bool {
        // Get the subpixel offsets in FT 26.6 format.
        let (dx, dy) = font.get_subpx_offset(key);
        let (dx, dy) = (
            (dx / scale as f64 * 64.0 + 0.5) as FT_Pos,
            -(dy / scale as f64 * 64.0 + 0.5) as FT_Pos,
        );

        // Move the outline curves to be at the origin, taking
        // into account the subpixel positioning.
        unsafe {
            let outline = &(*slot).outline;
            let mut cbox = FT_BBox { xMin: 0, yMin: 0, xMax: 0, yMax: 0 };
            FT_Outline_Get_CBox(outline, &mut cbox);
            Self::pad_bounding_box(font, &mut cbox);
            FT_Outline_Translate(
                outline,
                dx - ((cbox.xMin + dx) & !63),
                dy - ((cbox.yMin + dy) & !63),
            );
        }

        let render_mode = match font.render_mode {
            FontRenderMode::Mono => FT_Render_Mode::FT_RENDER_MODE_MONO,
            FontRenderMode::Alpha => FT_Render_Mode::FT_RENDER_MODE_NORMAL,
            FontRenderMode::Subpixel => if font.flags.contains(FontInstanceFlags::LCD_VERTICAL) {
                FT_Render_Mode::FT_RENDER_MODE_LCD_V
            } else {
                FT_Render_Mode::FT_RENDER_MODE_LCD
            },
        };
        let result = unsafe { FT_Render_Glyph(slot, render_mode) };
        if !succeeded(result) {
            error!("Unable to rasterize");
            debug!(
                "{:?} with {:?}, {:?}",
                key,
                render_mode,
                result
            );
            false
        } else {
            true
        }
    }

    pub fn begin_rasterize(font: &FontInstance) {
        // The global LCD filter state is only used in subpixel rendering modes.
        if font.render_mode == FontRenderMode::Subpixel {
            let mut cache = FONT_CACHE.lock().unwrap();
            let FontInstancePlatformOptions { lcd_filter, .. } = font.platform_options.unwrap_or_default();
            // Check if the current LCD filter matches the requested one.
            if cache.lcd_filter != lcd_filter {
                // If the filter doesn't match, we have to wait for all other currently rasterizing threads
                // that may use the LCD filter state to finish before we can override it.
                while cache.lcd_filter_uses != 0 {
                    cache = LCD_FILTER_UNUSED.wait(cache).unwrap();
                }
                // Finally set the LCD filter to the requested one now that the library is unused.
                cache.lcd_filter = lcd_filter;
                let filter = match lcd_filter {
                    FontLCDFilter::None => FT_LcdFilter::FT_LCD_FILTER_NONE,
                    FontLCDFilter::Default => FT_LcdFilter::FT_LCD_FILTER_DEFAULT,
                    FontLCDFilter::Light => FT_LcdFilter::FT_LCD_FILTER_LIGHT,
                    FontLCDFilter::Legacy => FT_LcdFilter::FT_LCD_FILTER_LEGACY,
                };
                unsafe {
                    let result = FT_Library_SetLcdFilter(cache.lib, filter);
                    // Setting the legacy filter may fail, so just use the default filter instead.
                    if !succeeded(result) {
                        FT_Library_SetLcdFilter(cache.lib, FT_LcdFilter::FT_LCD_FILTER_DEFAULT);
                    }
                }
            }
            cache.lcd_filter_uses += 1;
        }
    }

    pub fn end_rasterize(font: &FontInstance) {
        if font.render_mode == FontRenderMode::Subpixel {
            let mut cache = FONT_CACHE.lock().unwrap();
            // If this is the last use of the LCD filter, then signal that the LCD filter isn't used.
            cache.lcd_filter_uses -= 1;
            if cache.lcd_filter_uses == 0 {
                LCD_FILTER_UNUSED.notify_all();
            }
        }
    }

    pub fn rasterize_glyph(&mut self, font: &FontInstance, key: &GlyphKey) -> GlyphRasterResult {
        let (_cached, slot, scale) = self.load_glyph(font, key)
                                         .ok_or(GlyphRasterError::LoadFailed)?;

        // Get dimensions of the glyph, to see if we need to rasterize it.
        // Don't apply scaling to the dimensions, as the glyph cache needs to know the actual
        // footprint of the glyph.
        let dimensions = Self::get_glyph_dimensions_impl(slot, font, key, scale, false)
                             .ok_or(GlyphRasterError::LoadFailed)?;
        let GlyphDimensions { mut left, mut top, width, height, .. } = dimensions;

        // For spaces and other non-printable characters, early out.
        if width == 0 || height == 0 {
            return Err(GlyphRasterError::LoadFailed);
        }

        let format = unsafe { (*slot).format };
        match format {
            FT_Glyph_Format::FT_GLYPH_FORMAT_BITMAP => {}
            FT_Glyph_Format::FT_GLYPH_FORMAT_OUTLINE => {
                if !Self::rasterize_glyph_outline(slot, font, key, scale) {
                    return Err(GlyphRasterError::LoadFailed);
                }
            }
            _ => {
                error!("Unsupported format");
                debug!("format={:?}", format);
                return Err(GlyphRasterError::LoadFailed);
            }
        };

        debug!(
            "Rasterizing {:?} as {:?} with dimensions {:?}",
            key,
            font.render_mode,
            dimensions
        );

        let bitmap = unsafe { &(*slot).bitmap };
        let pixel_mode = unsafe { mem::transmute(bitmap.pixel_mode as u32) };
        let (mut actual_width, mut actual_height) = match pixel_mode {
            FT_Pixel_Mode::FT_PIXEL_MODE_LCD => {
                assert!(bitmap.width % 3 == 0);
                ((bitmap.width / 3) as usize, bitmap.rows as usize)
            }
            FT_Pixel_Mode::FT_PIXEL_MODE_LCD_V => {
                assert!(bitmap.rows % 3 == 0);
                (bitmap.width as usize, (bitmap.rows / 3) as usize)
            }
            FT_Pixel_Mode::FT_PIXEL_MODE_MONO |
            FT_Pixel_Mode::FT_PIXEL_MODE_GRAY |
            FT_Pixel_Mode::FT_PIXEL_MODE_BGRA => {
                (bitmap.width as usize, bitmap.rows as usize)
            }
            _ => panic!("Unsupported mode"),
        };

        // If we need padding, we will need to expand the buffer size.
        let (buffer_width, buffer_height, padding) = if font.use_texture_padding() {
            (actual_width + 2, actual_height + 2, 1)
        } else {
            (actual_width, actual_height, 0)
        };

        let mut final_buffer = vec![0u8; buffer_width * buffer_height * 4];

        // Extract the final glyph from FT format into BGRA8 format, which is
        // what WR expects.
        let subpixel_bgr = font.flags.contains(FontInstanceFlags::SUBPIXEL_BGR);
        let mut src_row = bitmap.buffer;
        let mut dest = 4 * padding * (padding + buffer_width);
        let actual_end = final_buffer.len() - 4 * padding * (buffer_width + 1);
        while dest < actual_end {
            let mut src = src_row;
            let row_end = dest + actual_width * 4;
            match pixel_mode {
                FT_Pixel_Mode::FT_PIXEL_MODE_MONO => {
                    while dest < row_end {
                        // Cast the byte to signed so that we can left shift each bit into
                        // the top bit, then right shift to fill out the bits with 0s or 1s.
                        let mut byte: i8 = unsafe { *src as i8 };
                        src = unsafe { src.offset(1) };
                        let byte_end = cmp::min(row_end, dest + 8 * 4);
                        while dest < byte_end {
                            let alpha = (byte >> 7) as u8;
                            final_buffer[dest + 0] = alpha;
                            final_buffer[dest + 1] = alpha;
                            final_buffer[dest + 2] = alpha;
                            final_buffer[dest + 3] = alpha;
                            dest += 4;
                            byte <<= 1;
                        }
                    }
                }
                FT_Pixel_Mode::FT_PIXEL_MODE_GRAY => {
                    while dest < row_end {
                        let alpha = unsafe { *src };
                        final_buffer[dest + 0] = alpha;
                        final_buffer[dest + 1] = alpha;
                        final_buffer[dest + 2] = alpha;
                        final_buffer[dest + 3] = alpha;
                        src = unsafe { src.offset(1) };
                        dest += 4;
                    }
                }
                FT_Pixel_Mode::FT_PIXEL_MODE_LCD => {
                    while dest < row_end {
                        let (mut r, g, mut b) = unsafe { (*src, *src.offset(1), *src.offset(2)) };
                        if subpixel_bgr {
                            mem::swap(&mut r, &mut b);
                        }
                        final_buffer[dest + 0] = b;
                        final_buffer[dest + 1] = g;
                        final_buffer[dest + 2] = r;
                        final_buffer[dest + 3] = max(max(b, g), r);
                        src = unsafe { src.offset(3) };
                        dest += 4;
                    }
                }
                FT_Pixel_Mode::FT_PIXEL_MODE_LCD_V => {
                    while dest < row_end {
                        let (mut r, g, mut b) =
                            unsafe { (*src, *src.offset(bitmap.pitch as isize),
                                      *src.offset((2 * bitmap.pitch) as isize)) };
                        if subpixel_bgr {
                            mem::swap(&mut r, &mut b);
                        }
                        final_buffer[dest + 0] = b;
                        final_buffer[dest + 1] = g;
                        final_buffer[dest + 2] = r;
                        final_buffer[dest + 3] = max(max(b, g), r);
                        src = unsafe { src.offset(1) };
                        dest += 4;
                    }
                    src_row = unsafe { src_row.offset((2 * bitmap.pitch) as isize) };
                }
                FT_Pixel_Mode::FT_PIXEL_MODE_BGRA => {
                    // The source is premultiplied BGRA data.
                    let dest_slice = &mut final_buffer[dest .. row_end];
                    let src_slice = unsafe { slice::from_raw_parts(src, dest_slice.len()) };
                    dest_slice.copy_from_slice(src_slice);
                }
                _ => panic!("Unsupported mode"),
            }
            src_row = unsafe { src_row.offset(bitmap.pitch as isize) };
            dest = row_end + 8 * padding;
        }

        if font.use_texture_padding() {
            left -= padding as i32;
            top += padding as i32;
            actual_width = buffer_width;
            actual_height = buffer_height;
        }

        match format {
            FT_Glyph_Format::FT_GLYPH_FORMAT_BITMAP => {
                if font.synthetic_italics.is_enabled() {
                    let (skew_buffer, skew_width, skew_left) = skew_bitmap(
                        &final_buffer,
                        actual_width,
                        actual_height,
                        left,
                        top,
                        font.synthetic_italics.to_skew(),
                        font.flags.contains(FontInstanceFlags::VERTICAL),
                    );
                    final_buffer = skew_buffer;
                    actual_width = skew_width;
                    left = skew_left;
                }
                if font.flags.contains(FontInstanceFlags::TRANSPOSE) {
                    final_buffer = transpose_bitmap(&final_buffer, actual_width, actual_height);
                    mem::swap(&mut actual_width, &mut actual_height);
                    mem::swap(&mut left, &mut top);
                    left -= actual_width as i32;
                    top += actual_height as i32;
                }
                if font.flags.contains(FontInstanceFlags::FLIP_X) {
                    flip_bitmap_x(&mut final_buffer, actual_width, actual_height);
                    left = -(left + actual_width as i32);
                }
                if font.flags.contains(FontInstanceFlags::FLIP_Y) {
                    flip_bitmap_y(&mut final_buffer, actual_width, actual_height);
                    top = -(top - actual_height as i32);
                }
            }
            FT_Glyph_Format::FT_GLYPH_FORMAT_OUTLINE => {
                unsafe {
                    left += (*slot).bitmap_left;
                    top += (*slot).bitmap_top - height as i32;
                }
            }
            _ => {}
        }

        let glyph_format = match (pixel_mode, format) {
            (FT_Pixel_Mode::FT_PIXEL_MODE_LCD, _) |
            (FT_Pixel_Mode::FT_PIXEL_MODE_LCD_V, _) => font.get_subpixel_glyph_format(),
            (FT_Pixel_Mode::FT_PIXEL_MODE_BGRA, _) => GlyphFormat::ColorBitmap,
            (_, FT_Glyph_Format::FT_GLYPH_FORMAT_BITMAP) => GlyphFormat::Bitmap,
            _ => font.get_alpha_glyph_format(),
        };

        Ok(RasterizedGlyph {
            left: left as f32,
            top: top as f32,
            width: actual_width as i32,
            height: actual_height as i32,
            scale,
            format: glyph_format,
            bytes: final_buffer,
        })
    }
}

