// Police: rigid-body ScadRig, shared bone names and clips with strike_raider.
use <strike_rig.scad>
$fn=10;
bone_root(0);
anim_idle=so_clip_idle();
anim_walk=so_clip_move();
anim_run=so_clip_move(0.60,0.26,0.07,0.16);
anim_crouch=so_clip_crouch();
anim_crouch_walk=so_clip_move(1.15,0.12,0.34,0.06);
anim_aim=so_ready();
anim_fire=so_clip_fire();
anim_reload=so_clip_reload();
anim_defuse=so_clip_use();
