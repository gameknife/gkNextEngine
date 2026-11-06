// Detachable parts: origin = mount. Coordinates match cw_wpn_*_held:
// +Z up, -Y muzzle. Empty optics use a HUD reticle; no opaque glass in ADS.
use <../lib/kit_coldwar.scad>

module nd_att_base() { cw_m_anod() cw_blk(0, 0.006, [0.040, 0.052, 0.012]); }
module nd_att_tube(f, len, r, z, wall = 0.004)
{
    difference()
    {
        cw_bore(f, len, r, z, fn = 24);
        cw_bore(f - 0.001, len + 0.002, r - wall, z, fn = 24);
    }
}
module nd_att_red_dot()
{
    nd_att_base();
    cw_m_anod() nd_att_tube(-0.030, 0.060, 0.028, 0.035);
    cw_m_anod() cw_blk(0, 0.035, [0.012, 0.022, 0.024], -0.032);
}
module nd_att_holographic()
{
    nd_att_base();
    cw_m_anod()
    {
        for (x = [-0.032, 0.032]) cw_blk(-0.015, 0.045, [0.007, 0.018, 0.060], x);
        cw_blk(-0.015, 0.075, [0.071, 0.018, 0.007]);
        cw_blk(-0.015, 0.015, [0.071, 0.035, 0.008]);
        cw_blk(0.024, 0.016, [0.065, 0.035, 0.026]);
    }
}
module nd_att_scope4x()
{
    nd_att_base();
    cw_m_anod()
    {
        nd_att_tube(-0.105, 0.225, 0.028, 0.038, 0.004);
        nd_att_tube(-0.108, 0.026, 0.034, 0.038, 0.010);
        nd_att_tube(0.095, 0.028, 0.037, 0.038, 0.009);
        cw_wf(0, 0.061) cw_cyl(0.020, 0.015);
    }
}
module nd_att_grip()
{
    cw_m_anod() cw_blk(0, 0, [0.033, 0.052, 0.017]);
    cw_m_rubber() cw_wf(0, -0.100) cw_cyl(0.095, 0.017);
    for (z = [-0.028, -0.048, -0.068, -0.088])
        cw_m_anod() cw_blk(0, z, [0.037, 0.025, 0.005]);
}
module nd_att_quick_mag()
{
    cw_m_rubber()
    {
        cw_blk(0, 0, [0.043, 0.066, 0.012]);
        cw_side_hole(0.022, [[-0.025, 0], [0.025, 0], [0.025, -0.035], [-0.025, -0.035]],
                           [[-0.015, -0.008], [0.015, -0.008], [0.015, -0.025], [-0.015, -0.025]]);
    }
}
module nd_att_extended_mag()
{
    cw_m_anod() cw_blk(0, -0.034, [0.037, 0.065, 0.075]);
    cw_m_rubber() cw_blk(0, -0.072, [0.043, 0.071, 0.012]);
}
module nd_att_suppressor()
{
    cw_m_anod() nd_att_tube(0, 0.15, 0.0225, 0, 0.0175);
    for (f = [0.02, 0.04, 0.06, 0.08, 0.10])
        cw_m_bright() nd_att_tube(f, 0.002, 0.023, 0, 0.002);
}
module nd_att_flashlight()
{
    cw_m_anod()
    {
        cw_bore(0, 0.10, 0.016, 0);
        cw_blk(0.025, 0.017, [0.032, 0.025, 0.014]);
        nd_att_tube(0.09, 0.018, 0.021, 0, 0.005);
    }
    // Deliberately unlit: lighting is a separate future feature.
    cw_m_lens() cw_bore(0.105, 0.001, 0.015, 0);
}

module bone_att_optic_red_dot() { gk_flatten() nd_att_red_dot(); }
module bone_att_optic_holographic() { gk_flatten() nd_att_holographic(); }
module bone_att_optic_4x() { gk_flatten() nd_att_scope4x(); }
module bone_att_grip_vertical() { gk_flatten() nd_att_grip(); }
module bone_att_mag_quick() { gk_flatten() nd_att_quick_mag(); }
module bone_att_mag_extended() { gk_flatten() nd_att_extended_mag(); }
module bone_att_muzzle_suppressor() { gk_flatten() nd_att_suppressor(); }
module bone_att_tactical_flashlight() { gk_flatten() nd_att_flashlight(); }
module bone_att_svd_scope() { gk_flatten() cw_svd_scope(); }
module bone_attachment_catalog()
{
    bone_att_optic_red_dot(); bone_att_optic_holographic(); bone_att_optic_4x();
    bone_att_grip_vertical(); bone_att_mag_quick(); bone_att_mag_extended();
    bone_att_muzzle_suppressor(); bone_att_tactical_flashlight(); bone_att_svd_scope();
}
bone_attachment_catalog();
