/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// This shader applies a (rounded) rectangle mask to the content of the framebuffer.

#include ps_quad,ellipse

varying highp vec4 vClipLocalPos;

#ifdef WR_FEATURE_FAST_PATH
flat varying highp vec4 v_clip_radii;
flat varying highp vec2 v_clip_size;
#else
flat varying highp vec4 vClipCenter_Radius_TL;
flat varying highp vec4 vClipCenter_Radius_TR;
flat varying highp vec4 vClipCenter_Radius_BR;
flat varying highp vec4 vClipCenter_Radius_BL;
// We pack 4 vec3 clip planes into 3 vec4 to save a varying slot.
flat varying highp vec4 vClipPlane_A;
flat varying highp vec4 vClipPlane_B;
flat varying highp vec4 vClipPlane_C;
#endif
flat varying highp vec2 vClipMode;

#ifdef WR_VERTEX_SHADER

PER_INSTANCE in ivec4 aClipData;

#define CLIP_SPACE_RASTER       0
#define CLIP_SPACE_PRIMITIVE    1

struct Clip {
    RectWithEndpoint rect;
#ifdef WR_FEATURE_FAST_PATH
    vec4 radii;
#else
    vec4 radii_top;
    vec4 radii_bottom;
#endif
    float mode;
    int space;
};

Clip fetch_clip(int index) {
    Clip clip;

    clip.space = aClipData.z;

#ifdef WR_FEATURE_FAST_PATH
    vec4 texels[3] = fetch_from_gpu_buffer_3f(index);
    clip.rect = RectWithEndpoint(texels[0].xy, texels[0].zw);
    clip.radii = texels[1];
    clip.mode = texels[2].x;
#else
    vec4 texels[4] = fetch_from_gpu_buffer_4f(index);
    clip.rect = RectWithEndpoint(texels[0].xy, texels[0].zw);
    clip.radii_top = texels[1];
    clip.radii_bottom = texels[2];
    clip.mode = texels[3].x;
#endif

    return clip;
}

void pattern_vertex(PrimitiveInfo prim_info) {

    Clip clip = fetch_clip(aClipData.y);
    Transform clip_transform = fetch_transform(aClipData.x);

    vClipLocalPos = clip_transform.m * vec4(prim_info.local_pos, 0.0, 1.0);

#ifndef WR_FEATURE_FAST_PATH
    if (clip.space == CLIP_SPACE_RASTER) {
        vTransformBounds = vec4(clip.rect.p0, clip.rect.p1);
    } else {
        RectWithEndpoint xf_bounds = RectWithEndpoint(
            max(clip.rect.p0, prim_info.local_clip_rect.p0),
            min(clip.rect.p1, prim_info.local_clip_rect.p1)
        );
        vTransformBounds = vec4(xf_bounds.p0, xf_bounds.p1);
    }
#endif

    vClipMode.x = clip.mode;

#ifdef WR_FEATURE_FAST_PATH
    // If the radii are uniform, we can use a simpler 2d signed distance
    // function to get a rounded rect clip.
    vec2 half_size = 0.5 * (clip.rect.p1 - clip.rect.p0);
    // Center the position in the box.
    vClipLocalPos.xy -= (half_size + clip.rect.p0) * vClipLocalPos.w;
    v_clip_size = half_size;
    v_clip_radii = clip.radii;
#else
    vec2 r_tl = clip.radii_top.xy;
    vec2 r_tr = clip.radii_top.zw;
    vec2 r_br = clip.radii_bottom.zw;
    vec2 r_bl = clip.radii_bottom.xy;

    vClipCenter_Radius_TL = vec4(clip.rect.p0 + r_tl,
                                 inverse_radii_squared(r_tl));

    vClipCenter_Radius_TR = vec4(clip.rect.p1.x - r_tr.x,
                                 clip.rect.p0.y + r_tr.y,
                                 inverse_radii_squared(r_tr));

    vClipCenter_Radius_BR = vec4(clip.rect.p1 - r_br,
                                 inverse_radii_squared(r_br));

    vClipCenter_Radius_BL = vec4(clip.rect.p0.x + r_bl.x,
                                 clip.rect.p1.y - r_bl.y,
                                 inverse_radii_squared(r_bl));

    // We need to know the half-spaces of the corners separate from the center
    // and radius. We compute a point that falls on the diagonal (which is just
    // an inner vertex pushed out along one axis, but not on both) to get the
    // plane offset of the half-space. We also compute the direction vector of
    // the half-space, which is a perpendicular vertex (-y,x) of the vector of
    // the diagonal. We leave the scales of the vectors unchanged.
    vec2 n_tl = -r_tl.yx;
    vec2 n_tr = vec2(r_tr.y, -r_tr.x);
    vec2 n_br = r_br.yx;
    vec2 n_bl = vec2(-r_bl.y, r_bl.x);
    vec3 tl = vec3(n_tl,
                   dot(n_tl, vec2(clip.rect.p0.x, clip.rect.p0.y + r_tl.y)));
    vec3 tr = vec3(n_tr,
                   dot(n_tr, vec2(clip.rect.p1.x - r_tr.x, clip.rect.p0.y)));
    vec3 br = vec3(n_br,
                   dot(n_br, vec2(clip.rect.p1.x, clip.rect.p1.y - r_br.y)));
    vec3 bl = vec3(n_bl,
                   dot(n_bl, vec2(clip.rect.p0.x + r_bl.x, clip.rect.p1.y)));

    vClipPlane_A = vec4(tl.x, tl.y, tl.z, tr.x);
    vClipPlane_B = vec4(tr.y, tr.z, br.x, br.y);
    vClipPlane_C = vec4(br.z, bl.x, bl.y, bl.z);
#endif

}
#endif

#ifdef WR_FRAGMENT_SHADER

#ifdef WR_FEATURE_FAST_PATH
// See https://www.shadertoy.com/view/4llXD7
// Notes:
//  * pos is centered in the origin (so 0,0 is the center of the box).
//  * The border radii must not be larger than half_box_size.
float sd_round_box(in vec2 pos, in vec2 half_box_size, in vec4 radii) {
    radii.xy = (pos.x > 0.0) ? radii.xy : radii.zw;
    radii.x  = (pos.y > 0.0) ? radii.x  : radii.y;
    vec2 q = abs(pos) - half_box_size + radii.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radii.x;
}
#endif

vec4 pattern_fragment(vec4 _base_color) {
    vec2 clip_local_pos = vClipLocalPos.xy / vClipLocalPos.w;
    float aa_range = compute_aa_range(clip_local_pos);

#ifdef WR_FEATURE_FAST_PATH
    float dist = sd_round_box(clip_local_pos, v_clip_size, v_clip_radii);
#else
    vec3 plane_tl = vec3(vClipPlane_A.x, vClipPlane_A.y, vClipPlane_A.z);
    vec3 plane_tr = vec3(vClipPlane_A.w, vClipPlane_B.x, vClipPlane_B.y);
    vec3 plane_br = vec3(vClipPlane_B.z, vClipPlane_B.w, vClipPlane_C.x);
    vec3 plane_bl = vec3(vClipPlane_C.y, vClipPlane_C.z, vClipPlane_C.w);

    float dist = distance_to_rounded_rect(
        clip_local_pos,
        plane_tl,
        vClipCenter_Radius_TL,
        plane_tr,
        vClipCenter_Radius_TR,
        plane_br,
        vClipCenter_Radius_BR,
        plane_bl,
        vClipCenter_Radius_BL,
        vTransformBounds
    );
#endif

    // Compute AA for the given dist and range.
    float alpha = distance_aa(aa_range, dist);

    // Select alpha or inverse alpha depending on clip in/out.
    float final_alpha = mix(alpha, 1.0 - alpha, vClipMode.x);

    return vec4(final_alpha);
}
#endif
