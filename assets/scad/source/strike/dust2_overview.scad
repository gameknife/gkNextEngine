// Roof-cut inspection scene only. The playable map retains tunnel roofs.
use <../../lib/kit_strike.scad>
use <../../lib/gk_camera.scad>
use <dust2_port.scad>
$fn=12;
ds_map(false);
gk_camera_lookat([53,58,175],[53,58,0],"Roof-cut plan",45);
