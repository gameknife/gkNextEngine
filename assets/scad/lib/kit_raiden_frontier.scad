// NextRaiden future-level scenery: desert energy, rail industry, polar research.
// Pure library, 36 public modules. Dependencies contain only shared authoring parts.
// 1 unit = 1m; Z-up; all vehicles and entrances face -Y (NextRaiden convention).
// Objects rest at z=0; 16x16 tiles have base top=0, bottom=-0.4, surface detail above.
// Railway gauge=1.8m; rail running surface z=0.18: place trains/buffers at z=0.18.
// All structures stay below the 7m flight plane at native scale.
// variant=0/1/2 changes structure, not just paint; seed is deterministic.
// These are static scenery, NOT registered enemies or detachable damage chunks.
// Flatten each placed object; the caller owns streaming and gameplay metadata.
use <raiden_parts.scad>
use <raiden_frontier_parts.scad>

// ================= Desert energy / mining =================
module rf_ground_desert(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        color(rf_SAND()) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (i = [0:3]) color(i%2 == 0 ? [0.35,0.30,0.20] : [0.29,0.26,0.18])
            translate([rf_rnd(seed+i*17,81)/10-4,rf_rnd(seed+i*31,81)/10-4,0])
                rotate([0,0,rf_rnd(seed+i*13,180)]) scale([1.5,0.7,1])
                    cylinder(h = 0.012, r = 1.4+rf_rnd(seed+i*7,8)/10, $fn = 7);
        if (v == 1) for (i = [0:5]) color([0.25,0.25,0.20])
            translate([rf_rnd(seed+i*43,121)/10-6,rf_rnd(seed+i*59,121)/10-6,0])
                cylinder(h = 0.12, r1 = 0.25, r2 = 0.1, $fn = 5);
        if (v == 2) for (i = [0:2]) color([0.37,0.32,0.22])
            translate([-4+i*3.5,-2+rf_rnd(seed+i*67,5),0]) rotate([0,0,22])
                scale([0.7,2.5,1]) cylinder(h = 0.06, r1 = 1, r2 = 0.6, $fn = 6);
    }
}

module rf_ground_saltflat(seed = 0)
{
    gk_flatten()
    {
        color(rf_SALT()) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (i = [0:4]) color(i%2 == 0 ? [0.35,0.37,0.33] : [0.42,0.43,0.37])
            translate([rf_rnd(seed+i*23,101)/10-5,rf_rnd(seed+i*41,101)/10-5,0])
                rotate([0,0,rf_rnd(seed+i*53,90)]) scale([1.3,0.8,1])
                    cylinder(h = 0.012+i*0.001, r = 1.6+rf_rnd(seed+i*11,7)/10, $fn = 5);
    }
}

module rf_ground_serviceway(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_ground_desert(seed);
        color([0.26,0.25,0.19]) translate([0,0,0.02]) nr_box([5.4,16,0.04]);
        for (x = [-1.6,1.6]) color([0.22,0.22,0.18]) translate([x,0,0.06]) nr_box([0.6,16,0.015]);
        if (v == 1) color([0.26,0.25,0.19]) translate([0,0,0.08]) nr_box([16,5.4,0.02]);
        if (v == 2) color([0.26,0.25,0.19]) translate([4,0,0.08]) nr_box([8,5.4,0.02]);
    }
}

module rf_bldg_refinery(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([9,7]);
        for (i = [0:1+v%2])
        {
            x = -2.8+i*2.6;
            h = 4.3+(i%2)*1.2;
            color(rf_WALL()) translate([x,1.5,0.18]) cylinder(h = h, r = 0.62, $fn = 10);
            for (z = [1.5,3.0]) color(rf_METAL()) translate([x,1.5,z]) cylinder(h = 0.18, r = 0.80, $fn = 10);
            color(rf_OCHRE()) translate([x,1.5,h+0.18]) cylinder(h = 0.20, r = 0.62, $fn = 10);
            color(rf_METAL()) rf_pipe([x,1.5,1.0],[x,-1.8,1.0]);
        }
        color(rf_TEAL()) translate([2.5,-1.9,0.18]) nr_bevel([2.5,2.2,1.8]);
        color(rf_METAL()) rf_pipe([-3.5,-1.8,1],[3.5,-1.8,1]);
        for (x = [-3,0,3]) color(rf_DARK()) translate([x,-1.8,0.18]) nr_box([0.25,0.65,0.68]);
        if (v == 2) color(rf_WALL()) translate([2.5,1.4,0.18]) cylinder(h = 2.1, r = 1.25, $fn = 10);
    }
}

module rf_bldg_coolingplant(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([10,7]);
        for (x = v == 0 ? [0] : [-2.5,2.5]) translate([x,0,0.18])
        {
            s = v == 0 ? 1 : 0.80;
            color(rf_WALL()) scale([s,s,s]) rotate_extrude($fn = 12)
                polygon([[2.5,0],[1.65,1.9],[1.4,3.8],[1.7,5.8],
                         [1.48,5.8],[1.18,3.8],[1.43,1.9],[2.28,0]]);
            color(rf_DARK()) cylinder(h = 0.06, r = 2.25*s, $fn = 12);
        }
        if (v == 2) color(rf_TEAL()) translate([0,-2.9,0.18]) nr_box([5.5,0.9,1.1]);
    }
}

module rf_bldg_drillsite(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([8,7]);
        color(rf_OCHRE()) translate([-1.5,0.5,0.18]) rf_tower(5.4,1.3);
        color(rf_DARK()) translate([-1.5,0.5,0.18]) cylinder(h = 5.2, r = 0.13, $fn = 6);
        translate([2.0,1.0,0.18]) rf_hut([2.4,3.5,1.5],rf_TEAL());
        for (i = [0:2+v]) color(rf_METAL())
            rf_pipe([-2.6+i*0.45,-2.5,0.5],[-2.6+i*0.45,-0.8,0.5],0.18);
        if (v == 1) color(rf_WALL()) translate([2,-2.1,0.18]) cylinder(h = 1.4, r = 0.75, $fn = 8);
        if (v == 2) color(rf_OCHRE()) translate([2,-2.1,0.18]) nr_box([2.0,1.4,0.7]);
    }
}

module rf_prop_pumpjack(variant = 0)
{
    v = rf_variant(variant);
    angle = [-12,0,12][v];
    gk_flatten()
    {
        rf_pad([3.6,6]);
        color(rf_METAL())
        {
            for (x = [-1.1,1.1]) rf_beam([x,0.4,0.29],[0,0.4,3.5],0.22);
            translate([0,0.4,3.5]) rotate([angle,0,0])
            {
                nr_box([0.42,4.1,0.35]);
                color(rf_OCHRE()) translate([-0.4,-2,-0.6]) rotate([90,0,90])
                    linear_extrude(height = 0.8)
                        polygon([[-0.45,0],[-0.45,1.2],[0.15,1.2],[0.50,0.8],[0.50,0.3],[0.15,0]]);
            }
        }
        headY = 0.4-2*cos(angle);
        headZ = 3.5-2*sin(angle);
        color(rf_METAL()) translate([0,headY,0.18]) cylinder(h = 0.35, r = 0.28, $fn = 8);
        color(rf_DARK()) rf_pipe([0,headY,0.3],[0,headY,headZ],0.075);
        color(rf_TEAL()) translate([0,2,0.18]) nr_box([1.6,1.4,0.8]);
        color(rf_OCHRE()) translate([0,1.25,1.5]) rotate([0,90,0]) cylinder(h = 0.40, r = 0.65, center = true, $fn = 8);
        color(rf_METAL()) rf_beam([0,1.25,1.5],[0,0.4+1.5*cos(angle),3.5+1.5*sin(angle)],0.14);
    }
}

module rf_prop_solararray(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        for (x = v == 2 ? [-2,2] : [-2.8,0,2.8], y = v == 0 ? [0] : [-2,2])
        {
            color(rf_METAL()) translate([x,y,0]) nr_box([0.24,0.6,1.1]);
            translate([x,y,1.1]) rotate([22,0,0])
            {
                color(rf_METAL()) nr_box([2.4,2.8,0.12]);
                color([0.055,0.13,0.24]) translate([0,0,0.12]) nr_box([2.2,2.6,0.04]);
                for (i = [-0.65,0.65]) color([0.15,0.24,0.31]) translate([0,i,0.16]) nr_box([2.2,0.035,0.015]);
            }
        }
        if (v == 2) color(rf_WALL()) translate([0,0,0]) nr_box([1,1.4,1.3]);
    }
}

// Ports at y=-8/+8 (straight), y=-8/x=+8 (elbow), and all three (tee).
module rf_prop_pipeline(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        color(rf_METAL())
        {
            rf_pipe([0,-8,1.25],[0,v == 1 ? 0 : 8,1.25],0.28);
            if (v != 0) rf_pipe([0,0,1.25],[8,0,1.25],0.28);
            if (v != 0) translate([0,0,1.25]) sphere(r = 0.28, $fn = 8);
        }
        for (y = v == 1 ? [-6,-2] : [-6,-2,2,6]) color(rf_WALL()) translate([0,y,0]) nr_box([1.0,0.32,1.02]);
        if (v != 0) for (x = [2,6]) color(rf_WALL()) translate([x,0,0]) nr_box([0.32,1.0,1.02]);
        color(rf_OCHRE()) translate([0,-2,1.25]) cylinder(h = 0.75, r = 0.10, $fn = 6);
        color(rf_OCHRE()) translate([0,-2,1.92]) difference()
        {
            cylinder(h = 0.12, r = 0.45, $fn = 8);
            translate([0,0,-0.02]) cylinder(h = 0.16, r = 0.30, $fn = 8);
        }
        color(rf_OCHRE()) translate([0,-2,1.94]) nr_box([0.85,0.10,0.08]);
    }
}

module rf_bldg_desertcompound(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([10,9]);
        color([0.34,0.31,0.23])
        {
            translate([-3.2,0,0.18]) nr_box([3,8,2.5]);
            translate([0,2.5,0.18]) nr_box([9.4,3,2.8]);
            if (v != 0) translate([3.2,-0.7,0.18]) nr_box([3,3.4,2.2]);
        }
        for (y = [-2,0.5]) color(rf_GLASS()) translate([-1.68,y,1.4]) nr_box([0.045,0.65,0.5]);
        color(rf_TEAL()) translate([1,0.97,0.18]) nr_box([1.1,0.07,1.7]);
        color(rf_WALL()) translate([-3.2,1.8,2.68]) cylinder(h = 0.9, r = 0.70, $fn = 8);
        if (v == 2)
        {
            color(rf_METAL()) translate([0,-2.5,2]) nr_box([3.6,2,0.15]);
            for (x = [-1.5,1.5]) color(rf_METAL()) translate([x,-3.2,0.18]) nr_box([0.15,0.15,1.82]);
        }
    }
}

module rf_nature_mesa(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        for (i = [0:2]) color(i == 1 ? [0.27,0.23,0.18] : [0.36,0.30,0.21])
            translate([i*0.20,0,i*1.5]) rotate([0,0,rf_rnd(seed,30)]) scale([1.25,0.85,1])
                cylinder(h = 1.52, r1 = 3.4-i*0.6, r2 = 3.1-i*0.6, $fn = 6+v);
        if (v == 1) color([0.30,0.27,0.20]) translate([-3.8,0,0])
            cylinder(h = 2.6, r1 = 1.5, r2 = 0.8, $fn = 5);
        if (v == 2) color([0.38,0.33,0.24]) translate([0.4,0,4.5])
            cylinder(h = 1.2, r1 = 1.7, r2 = 1.3, $fn = 7);
    }
}

module rf_veh_haultruck(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_wheels(4.9,3.0,0.65);
        color(rf_METAL()) translate([0,0,0.80]) nr_box([3.0,5.4,0.40]);
        color(rf_OCHRE()) translate([-0.55,-1.9,1.2]) nr_bevel([1.8,1.6,1.5]);
        color(rf_GLASS()) translate([-0.55,-2.72,1.85]) nr_box([1.5,0.04,0.65]);
        color(rf_METAL()) translate([1,-1.9,1.2]) nr_box([0.9,1.6,0.65]);
        color(rf_OCHRE()) translate([0,0.7,1.2])
        {
            nr_box([3.3,3.5,0.20]);
            for (x = [-1.5,1.5]) translate([x,0,0.20]) nr_box([0.30,3.5,1.0]);
            for (y = [-1.6,1.6]) translate([0,y,0.20]) nr_box([3.3,0.30,1.0]);
        }
        if (v == 1) color([0.25,0.24,0.20]) translate([0,0.7,1.4])
            scale([1,1.15,1]) cylinder(h = 1.1, r1 = 1.3, r2 = 0.7, $fn = 6);
        if (v == 2) color(rf_TEAL()) translate([0,0.7,2.4]) nr_bevel([3.3,3.5,0.25]);
    }
}

// ================= Rail / urban industry =================
module rf_ground_rail(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        color([0.20,0.24,0.21]) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (cx = v == 1 ? [-3,3] : [0])
        {
            color([0.26,0.28,0.26]) translate([cx,0,0]) nr_box([3.8,16,0.05]);
            for (y = [-7.5:1:7.5]) color([0.14,0.17,0.16]) translate([cx,y,0.05]) nr_box([2.8,0.22,0.07]);
            for (x = [-0.9,0.9]) color(rf_METAL()) translate([cx+x,0,0.12]) nr_box([0.12,16,0.06]);
        }
        color([0.18,0.22,0.20]) translate([v == 1 ? 6.2 : 4.2,rf_rnd(seed,81)/10-4,0]) nr_box([1.8,3.5,0.012]);
        if (v == 2) color([0.23,0.26,0.25]) translate([0,0,0.05]) nr_box([16,3.4,0.055]);
    }
}

module rf_ground_industrial(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        color([0.18,0.22,0.22]) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        color([0.15,0.19,0.19]) translate([0,rf_rnd(seed,101)/10-5,0]) nr_box([16,0.045,0.012]);
        if (rf_rnd(seed+15,3) == 0) color([0.20,0.23,0.23])
            translate([rf_rnd(seed+11,81)/10-4,rf_rnd(seed+27,101)/10-5,0])
                nr_box([2+rf_rnd(seed+33,3),2+rf_rnd(seed+39,3),0.013]);
        if (rf_rnd(seed+51,3) != 0) color([0.13,0.17,0.17])
            translate([rf_rnd(seed+61,2) == 0 ? -6.5 : 6.5,rf_rnd(seed+73,121)/10-6,0])
                nr_box([0.35,0.9,0.014]);
        if (v == 1) for (y = [-5,0,5]) color([0.35,0.34,0.22]) translate([3,y,0.02]) nr_box([8,0.1,0.015]);
        if (v == 2) for (x = [-2.5,2.5]) color([0.35,0.34,0.22]) translate([x,0,0.02]) nr_box([0.12,16,0.015]);
    }
}

module rf_bldg_roundhouse(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([17,11]);
        // Splayed bays share a virtual turntable south of the module.
        for (a = v == 0 ? [-22,22] : [-30,0,30])
            translate([0,-8.7,0.18]) rotate([0,0,a]) translate([0,10.3,0])
            {
                color(rf_WALL()) translate([0,2.7,0]) nr_box([3.3,0.25,2.7]);
                for (x = [-1.55,1.55]) color(rf_WALL()) translate([x,0,0]) nr_box([0.2,5.6,2.7]);
                color(v == 2 ? rf_TEAL() : rf_METAL()) translate([0,0,2.7]) rf_roof(3.6,5.9,0.6);
                for (x = [-0.9,0.9]) color(rf_METAL()) translate([x,-0.4,0]) nr_box([0.12,6.4,0.12]);
                if (v == 2) color(rf_DARK()) translate([0,-2.8,0]) nr_box([2.9,0.15,2.5]);
            }
    }
}

module rf_bldg_freightterminal(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([11,8]);
        color(rf_WALL()) translate([0,0.9,0.18]) nr_box([10,4.5,2.6]);
        color(rf_METAL()) translate([0,0.4,2.78]) rf_roof(10.5,6.0,0.6);
        color([0.27,0.29,0.26]) translate([0,-2.2,0.18]) nr_box([10.5,2.8,0.55]);
        for (x = [-3.3,0,3.3])
        {
            color(rf_TEAL()) translate([x,-1.4,0.73]) nr_box([2,0.08,1.8]);
            if (v != 0) color(rf_OCHRE()) translate([x,-2.6,0.73]) nr_box([1.4,1.0,v == 1 ? 0.7 : 1.1]);
        }
    }
}

module rf_bldg_machinehall(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([10,9]);
        color(rf_WALL()) translate([0,0,0.18]) nr_box([9,8,2.6]);
        for (x = [-3,0,3])
        {
            color(rf_METAL()) translate([x,0,2.78]) hull()
            {
                nr_box([3.0,8.3,0.1]);
                translate([1.35,0,1.1]) nr_box([0.25,8.3,0.1]);
            }
            color(rf_GLASS()) translate([x+1.49,0,3.0]) nr_box([0.025,7.6,0.7]);
        }
        color(rf_DARK()) translate([0,-4.025,0.18]) nr_box([2.5,0.08,2.0]);
        for (x = v == 1 ? [-3,3] : [3]) color(v == 2 ? rf_OCHRE() : rf_METAL())
            translate([x,2.4,3.5]) cylinder(h = v == 2 ? 2.2 : 1.3, r = 0.32, $fn = 8);
    }
}

module rf_bldg_cityblock(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([11,10]);
        for (x = v == 0 ? [-3.2] : [-3.2,3.2])
        {
            color(x < 0 ? [0.25,0.30,0.28] : [0.30,0.29,0.26]) translate([x,0,0.18]) nr_box([3.5,9,4.7]);
            color(rf_METAL()) translate([x,0,4.88]) nr_box([3.7,9.2,0.16]);
            for (z = [1.4,3.1], y = [-2.8,0,2.8]) color(rf_GLASS())
                translate([x+(x < 0 ? -1.77 : 1.77),y,z]) nr_box([0.04,1.2,0.65]);
            color(rf_WALL()) translate([x,2,5.04]) nr_box([1.3,1.6,0.45]);
        }
        color([0.32,0.33,0.28]) translate([0,3.1,0.18]) nr_box([3.0,2.8,3.0]);
        if (v == 2) color(rf_TEAL()) translate([0,-3.0,0.18]) nr_box([3,2.6,2]);
        color(rf_DARK()) translate([-3.2,-4.53,0.18]) nr_box([1.1,0.06,1.9]);
    }
}

module rf_prop_signalbridge(variant = 0)
{
    v = rf_variant(variant);
    w = v == 0 ? 5 : 10;
    gk_flatten()
    {
        for (x = [-w/2,w/2])
        {
            color(rf_WALL()) translate([x,0,0]) nr_box([0.8,1,0.3]);
            color(rf_METAL()) translate([x,0,0.3]) nr_box([0.22,0.30,4.8]);
        }
        color(rf_METAL()) translate([0,0,4.5]) nr_box([w,0.45,0.6]);
        for (x = v == 0 ? [0] : [-3,3])
        {
            color(rf_DARK()) translate([x,-0.34,3.75]) nr_bevel([0.65,0.45,1.2]);
            for (z = [4.05,4.48]) color(v == 2 ? rf_OCHRE() : rf_TEAL())
                translate([x,-0.58,z]) rotate([90,0,0]) cylinder(h = 0.05, r = 0.16, $fn = 8);
        }
    }
}

module rf_prop_railbuffer(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        for (x = [-0.9,0.9])
        {
            color(rf_METAL()) translate([x,0,0]) nr_box([0.25,2.4,0.2]);
            color(rf_METAL()) rf_beam([x,1,0.2],[x,-0.5,1.1],0.22);
        }
        color(v == 1 ? rf_WALL() : rf_OCHRE()) translate([0,-0.5,0.95]) nr_box([2.7,0.4,0.38]);
        if (v == 1) color(rf_WALL()) translate([0,0.35,0]) nr_box([2.6,1.3,0.95]);
        for (x = [-0.9,0.9]) color(rf_DARK()) translate([x,-0.8,1.12]) rotate([90,0,0])
            cylinder(h = v == 2 ? 0.6 : 0.25, r = 0.24, $fn = 8);
    }
}

module rf_veh_locomotive(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_railchassis(8);
        hoodHeight = v == 1 ? 1.15 : 1.9;
        color(v == 1 ? rf_OCHRE() : rf_TEAL()) translate([0,0.6,0.9]) nr_bevel([2.2,6.2,hoodHeight]);
        color(rf_WALL()) translate([0,-2.3,0.9]) nr_bevel([2.5,2,2.5]);
        color(rf_GLASS()) translate([0,-3.32,2.2]) nr_box([2.1,0.045,0.7]);
        for (x = [-1.12,1.12], y = [-0.3,1.8]) color(rf_DARK()) translate([x,y,1.2]) nr_box([0.05,1.3,0.65]);
        for (y = v == 1 ? [1.2] : [0.2,2.2]) color(rf_METAL()) translate([0,y,0.9+hoodHeight])
            cylinder(h = 0.25, r = 0.58, $fn = 8);
        if (v == 2) color(rf_METAL()) translate([0,0.7,2.8]) nr_box([1.6,3.2,0.65]);
        color(rf_OCHRE()) translate([0,-3.8,1.0]) nr_box([2.5,0.25,0.3]);
    }
}

module rf_veh_freightcar(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_railchassis();
        if (v == 0)
        {
            color(rf_TEAL()) translate([0,0,0.9]) nr_box([2.5,6.7,2.1]);
            color(rf_METAL()) translate([0,0,3.0]) rf_roof(2.65,6.9,0.35);
            for (x = [-1.27,1.27]) color(rf_WALL()) translate([x,0,1.05]) nr_box([0.06,1.5,1.85]);
        }
        if (v == 1)
        {
            color(rf_OCHRE()) translate([0,0,0.9]) nr_box([2.5,6.7,0.2]);
            for (y = [-2.3,0,2.3]) color(rf_WALL()) translate([0,y,1.1]) nr_box([1.9,1.65,1.3]);
        }
        if (v == 2)
        {
            color(rf_WALL()) rf_pipe([0,-2.9,1.95],[0,2.9,1.95],1.0);
            for (y = [-2,2]) color(rf_METAL()) translate([0,y,0.9]) nr_box([2.3,0.35,0.7]);
            color(rf_OCHRE()) translate([0,0,2.95]) cylinder(h = 0.25, r = 0.36, $fn = 8);
        }
    }
}

module rf_prop_cableyard(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([5.4,5.2]);
        for (x = [-1.3,1.3], y = v == 0 ? [0] : [-1.2,1.2])
            translate([x,y,0.18]) rotate([0,0,v == 2 ? 90 : 0]) rf_reel(v == 2 ? 0.8 : 0.65);
    }
}

module rf_prop_scrapyard(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([7.6,6.4]);
        for (i = [0:4]) color(i%2 == 0 ? rf_METAL() : rf_TEAL())
            translate([-2.7+rf_rnd(seed+i*17,55)/10,-2+rf_rnd(seed+i*31,41)/10,0.18])
                rotate([0,0,rf_rnd(seed+i*47,180)]) nr_bevel([1.6,0.8,0.35+i*0.08]);
        for (i = [0:1+v]) color(rf_WALL()) translate([-2+i*1.3,2.4,0.18]) nr_box([1,0.8,1.0]);
        color(rf_OCHRE()) translate([0,-2.8,0.18]) nr_box([6.8,0.18,0.9]);
    }
}

// ================= Polar research / frozen coast =================
module rf_ground_snow(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        color(rf_SNOW()) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (i = [0:3]) color(i%2 == 0 ? [0.35,0.42,0.44] : [0.43,0.47,0.47])
            translate([rf_rnd(seed+i*31,91)/10-4.5,rf_rnd(seed+i*53,91)/10-4.5,0])
                rotate([0,0,rf_rnd(seed+i*7,180)]) scale([1.4,0.65,1])
                    cylinder(h = 0.012+i*0.001, r = 2.0, $fn = 7);
        if (v == 1) for (x = [-1,1]) color([0.32,0.39,0.41]) translate([x,0,0.02]) nr_box([0.6,16,0.012]);
        if (v == 2) for (i = [0:2]) color([0.23,0.30,0.32])
            translate([-4+i*3.5,-2+rf_rnd(seed+i*29,5),0]) cylinder(h = 0.12, r1 = 0.65, r2 = 0.35, $fn = 5);
    }
}

module rf_ground_ice(seed = 0)
{
    gk_flatten()
    {
        gk_material(rf_ICE(), roughness = 0.30) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (i = [0:2])
        {
            x = rf_rnd(seed+i*37,61)/10-3;
            y = rf_rnd(seed+i*71,81)/10-4;
            color([0.22,0.38,0.42]) translate([x,y,0]) rotate([0,0,rf_rnd(seed+i*23,180)])
                linear_extrude(height = 0.015+i*0.001)
                    polygon([[-3,-0.15],[-0.4,0.05],[1,0.9],[3.2,0.55],[1.0,1.03],[-0.5,0.16],[-3,-0.05]]);
        }
    }
}

module rf_ground_frozenroad(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_ground_snow(seed);
        color([0.20,0.28,0.30]) translate([0,0,0.02]) nr_box([5.6,16,0.04]);
        for (x = [-3.3,3.3]) color([0.43,0.48,0.48]) translate([x,0,0.02]) nr_box([0.65,16,0.16]);
        if (v == 1) for (y = [-6,-2,2,6]) color([0.34,0.39,0.36]) translate([0,y,0.06]) nr_box([0.15,1.5,0.015]);
        if (v == 2) for (x = [-1.4,1.4]) color([0.15,0.23,0.26]) translate([x,0,0.06]) nr_box([0.6,16,0.015]);
    }
}

module rf_bldg_polarstation(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        for (x = v == 0 ? [-2.5,2.5] : [-3,3])
        {
            for (dx = [-1,1], y = [-2,2]) color(rf_METAL()) translate([x+dx,y,0]) nr_box([0.25,0.3,0.9]);
            translate([x,0,0.8]) rf_hut([3,5.2,1.9],x < 0 ? rf_TEAL() : rf_WALL(),true);
            color(rf_METAL()) translate([x,-3.05,0]) nr_box([1.25,0.8,0.4]);
            color(rf_METAL()) translate([x,-2.8,0.4]) nr_box([1.25,0.5,0.4]);
        }
        color(rf_WALL()) translate([0,1,0.8]) nr_box([v == 0 ? 2 : 3,1.4,1.4]);
        color(rf_SNOW()) translate([0,1,2.2]) nr_box([v == 0 ? 2 : 3,1.5,0.12]);
        if (v == 2) color(rf_OCHRE()) translate([0,-1.5,0]) nr_box([1.4,1.8,0.9]);
    }
}

module rf_bldg_radome(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([v == 0 ? 7 : 10,7]);
        for (x = v == 0 ? [0] : [-2.5,2.5])
        {
            r = v == 0 ? 2.8 : 1.9;
            color(rf_TEAL()) translate([x,0,0.18]) cylinder(h = 1.5, r = r*0.86, $fn = 12);
            color(rf_SNOW()) translate([x,0,1.68]) intersection()
            {
                sphere(r = r, $fn = 12);
                translate([0,0,r/2]) cube([r*2.1,r*2.1,r], center = true);
            }
            color(rf_DARK()) translate([x,-r*0.86,0.18]) nr_box([0.9,0.08,1.2]);
        }
        if (v == 2) color(rf_OCHRE()) translate([0,-2,0.18]) nr_box([1.1,1.6,1.5]);
    }
}

module rf_prop_disharray(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([v == 0 ? 5 : 8,5]);
        for (x = v == 0 ? [0] : [-2.1,2.1]) translate([x,0,0.18]) rotate([0,0,v == 2 ? x*18 : 0])
            rf_dish(v == 0 ? 2.0 : 1.65,v == 1 ? 40 : 25);
    }
}

module rf_bldg_generatorbank(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([8,6]);
        for (x = v == 0 ? [-1.7,1.7] : [-2.5,0,2.5])
        {
            color(rf_TEAL()) translate([x,0,0.18]) nr_bevel([1.9,4.5,1.7]);
            color(rf_SNOW()) translate([x,0.2,1.88]) nr_box([2,4.2,0.14]);
            color(rf_DARK()) translate([x,-2.27,0.6]) nr_box([1.4,0.04,0.85]);
            color(rf_METAL()) translate([x,1.2,1.9]) cylinder(h = v == 2 ? 1.5 : 0.8, r = 0.18, $fn = 6);
        }
    }
}

module rf_bldg_weatherlab(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([7.5,7]);
        color(rf_WALL()) translate([-0.9,0,0.18]) cylinder(h = 2.2, r = 2.25, $fn = 6);
        color(rf_SNOW()) translate([-0.9,0,2.38]) cylinder(h = 0.65, r1 = 2.45, r2 = 1, $fn = 6);
        color(rf_GLASS()) translate([-0.9,-1.97,1.3]) nr_box([2.1,0.06,0.65]);
        color(rf_METAL()) translate([2.7,1.8,0.18]) cylinder(h = 5.5, r = 0.09, $fn = 6);
        color(rf_METAL()) translate([2.7,1.8,4.7]) nr_box([1.5,0.10,0.10]);
        for (x = [2.1,3.3]) color(rf_OCHRE()) translate([x,1.8,4.8]) cylinder(h = 0.24, r = 0.22, $fn = 6);
        color(rf_TEAL()) translate([2.7,1.8,2.4]) nr_box([0.7,0.8,0.65]);
        if (v == 1) color(rf_SNOW()) translate([-0.9,0,3.03]) sphere(r = 0.65, $fn = 10);
        if (v == 2) color(rf_OCHRE()) translate([1.6,-2.5,0.18]) nr_box([2,1.0,1.1]);
    }
}

module rf_prop_supplycache(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        rf_pad([5.4,4.8]);
        for (x = [-1.5,0,1.5], y = [-0.8,0.8]) color(x == 0 ? rf_OCHRE() : rf_TEAL())
            translate([x,y,0.18]) nr_box([1.25,1.2,x == 0 ? 1.2 : 0.8]);
        if (v == 1)
        {
            for (x = [-2.1,2.1], y = [-1.2,1.2]) color(rf_METAL())
                translate([x,y,0.18]) nr_box([0.12,0.12,1.22]);
            color(rf_SNOW()) translate([0,0,1.4]) rf_roof(4.6,3.0,0.45);
        }
        if (v == 2)
        {
            color(rf_WALL()) translate([0,2.1,0.18]) nr_box([5,0.2,1.5]);
            for (x = [-2.4,2.4]) color(rf_WALL()) translate([x,0.8,0.18]) nr_box([0.2,2.8,1.5]);
        }
    }
}

module rf_veh_snowcat(variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        for (x = [-1.3,1.3])
        {
            color(rf_DARK()) translate([x,0,0]) nr_bevel([0.65,4.6,0.7]);
            for (y = [-1.6,-0.8,0,0.8,1.6]) color(rf_METAL()) translate([x,y,0.7]) nr_box([0.67,0.16,0.06]);
        }
        color(rf_TEAL()) translate([0,0,0.5]) nr_box([2.5,4.1,0.6]);
        color(rf_OCHRE()) translate([0,-1.1,1.1]) nr_bevel([2.3,1.9,1.45]);
        color(rf_GLASS()) translate([0,-2.07,1.65]) nr_box([2,0.045,0.65]);
        color(v == 1 ? rf_SNOW() : rf_TEAL()) translate([0,1.1,1.1]) nr_bevel([2.2,2, v == 1 ? 1.3 : 0.7]);
        if (v == 2)
        {
            color(rf_METAL()) rf_beam([0,-1.7,0.4],[0,-2.9,0.4],0.22);
            color(rf_OCHRE()) translate([0,-2.9,0.12]) nr_bevel([4,0.4,0.85]);
        }
    }
}

module rf_nature_iceberg(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        color(rf_ICE()) scale([1.4,0.85,1]) cylinder(h = 1.0, r1 = 3.4, r2 = 3, $fn = 7);
        for (i = [0:1+v%2]) color(i%2 == 0 ? rf_SNOW() : [0.29,0.41,0.44])
            translate([-1.3+i*1.8,rf_rnd(seed+i*19,15)/10-0.7,0.9])
                rotate([0,0,rf_rnd(seed+i*41,90)]) hull()
                {
                    cylinder(h = 0.1, r = 1.65, $fn = 5);
                    translate([0.3,-0.15,2.5+(v == 2 ? 1.8 : i*0.6)]) cylinder(h = 0.1, r = 0.35, $fn = 5);
                }
    }
}

module rf_nature_snowpines(seed = 0, variant = 0)
{
    v = rf_variant(variant);
    gk_flatten()
    {
        for (i = [0:2+v%2])
            translate([(i%2)*3-1.5,floor(i/2)*3-1.5,0]) rotate([0,0,rf_rnd(seed+i*17,60)])
                rf_pine(3.2+rf_rnd(seed+i*41,14)/10+(v == 2 ? 0.2 : 0));
    }
}
