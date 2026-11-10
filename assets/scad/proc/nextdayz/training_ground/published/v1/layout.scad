// NextDayz training ground: gameplay-first layout over synthetic ground (no
// real coordinates). An army training area behind a concrete gate: HQ and
// parade ground, armory, quartermaster store, mess tent, field stores and
// barracks around a gravel street grid; the rifle range runs 300 m north into
// a wooded hill; obstacle course west, shoot house north, motor pool south-
// west, a closed infected pen east. Every item in the game is on display.
// [id,name,kind,center,padSize,rotation,buildingCount,risk(1..3)]
PCG_SITES = [
  ["gate","Main gate","tg_gate",[-60,-260],[70,50],0,2,1],
  ["hq","Range control and parade ground","tg_hq",[-60,-175],[110,70],0,2,1],
  ["armory","Armory","tg_armory",[-165,-90],[60,48],0,2,1],
  ["quartermaster","Quartermaster store","tg_quartermaster",[-85,-90],[64,60],0,1,1],
  ["mess","Mess tent and field kitchen","tg_mess",[-165,-15],[56,48],0,1,1],
  ["stores","Field stores","tg_stores",[-85,-15],[56,48],0,1,1],
  ["barracks","Barracks","tg_barracks",[-110,75],[140,80],0,4,1],
  ["range","Rifle range","tg_range",[60,120],[110,380],0,3,1],
  ["obstacle","Obstacle course","tg_obstacle",[-270,0],[60,170],0,1,1],
  ["killhouse","Shoot house","tg_killhouse",[-50,182],[50,46],0,1,1],
  ["motorpool","Motor pool","tg_motorpool",[-200,-175],[90,70],0,2,1],
  ["pen","Infected pen","tg_pen",[290,200],[60,60],0,1,3]
];
// Gravel roads: approach through the gate, the camp street grid, tracks to
// the range, the pen and the helipads.
PCG_PATHS = [
  ["road",[[-60,-398],[-60,-212]],8],
  ["road",[[-250,-130],[40,-130]],7],
  ["road",[[-125,-130],[-125,30]],6],
  ["road",[[-20,-130],[-20,155]],5],
  ["road",[[40,-130],[40,-75]],6],
  ["road",[[-250,-130],[-250,-88]],5],
  ["road",[[40,-90],[150,-90],[230,60],[270,165]],4],
  ["road",[[40,-130],[175,-150]],5],
  ["road",[[-180,30],[-260,120],[-290,225]],4]
];
PCG_SPAWNS = [[-60,-350],[20,-340],[-150,-335],[100,-310]];
PCG_EXTRACTIONS = [[200,-150],[-300,250]];
// Sparse ruderal scrub on the open training ground.
PCG_VEGETATION = [["scrub",[-380,-380,380,380],260,28,[0.8,1.3],"shrub"]];
// Keep the lines of fire and the gate approach clear.
PCG_CLEAR = [[[60,-60],[60,320],110],[[-60,-398],[-60,-212],24],[[40,-90],[270,165],14]];
// Pine and spruce woods ring the area; birch scrub on the old clearings.
PCG_LANDCOVER = [
  ["west_pines","pine",[[-392,-392],[-312,-392],[-312,392],[-392,392]]],
  ["north_spruce","spruce",[[-312,320],[392,320],[392,392],[-312,392]]],
  ["east_mixed","mixed",[[332,-392],[392,-392],[392,320],[332,320]]],
  ["sw_pines","pine",[[-312,-392],[-110,-392],[-110,-312],[-240,-280],[-312,-250]]],
  ["se_mixed","mixed",[[5,-392],[332,-392],[332,-240],[150,-230],[40,-300],[5,-330]]],
  ["nw_spruce","spruce",[[-312,110],[-200,135],[-100,215],[-90,320],[-312,320]]],
  ["range_east_pines","pine",[[135,-40],[230,-40],[250,90],[200,160],[230,320],[135,320]]],
  ["pen_clearing","clearing",[[240,100],[332,100],[332,320],[240,320]]],
  ["east_clearing","clearing",[[150,-220],[332,-220],[332,90],[250,90],[230,-40],[150,-60]]]
];
// Birch rows along the camp street.
PCG_ALLEYS = [[[[-250,-126],[40,-126]],"birch",11,5],[[[-121,-126],[-121,28]],"birch",12,4]];
