// Otaru v4: regional street terraces, stone-store frontage and open quays.
// Geographic skeleton: north canal + harbour; 14 sites / 120 buildings.
// Research and spatial rules: docs/projects/nextdayz/otaru-v4-authoring.md.
// Site=[id,name,kind,center,size,rotation,buildingCount,risk].

PCG_SITES = [
    ["south_housing","Minami timber-house lanes","hokkaido_lane",[-350,-340],[210,78],0,16,1],
    ["central_housing","Ironai backstreet homes","hokkaido_lane",[-350,-125],[210,78],0,16,1],
    ["hill_housing","Temiya hillside homes","hokkaido_lane",[-350,85],[195,78],-8,14,1],
    ["ironai_west","Ironai old shopping street","shotengai",[-340,-235],[210,48],0,16,2],
    ["market_street","Canal approach shops","shotengai",[-130,-175],[150,48],0,12,2],
    ["temiya_shops","Temiya neighbourhood shops","shotengai",[-330,180],[190,48],0,12,1],
    ["canal_warehouses","North canal stone stores","canal",[-150,200],[210,62],90,6,2],
    ["north_quay","Ironai cargo quay","portyard",[180,325],[150,110],0,6,2],
    ["southern_freight","South freight repair yard","portyard",[270,-390],[130,100],0,4,2],
    ["shipping_office","Former shipping office and stores","portyard",[-145,365],[100,100],0,4,2],
    ["clinic","Emergency clinic","medical",[-130,20],[110,72],0,4,2],
    ["police","South koban cordon","checkpoint",[-130,-410],[105,72],0,4,2],
    ["port_command","Port quarantine command","military",[-130,-315],[100,76],0,4,3],
    ["shrine","Hillside neighbourhood shrine","shrine",[-340,300],[70,80],0,2,1]
];

PCG_SPAWNS = [
    [-470,-470],
    [-470,330],
    [-170,480],
    [-40,-470],
    [430,-470]
];

PCG_EXTRACTIONS = [
    [-460,250],
    [430,-350]
];

PCG_PATHS = [
    ["road",[[-450,-440],[-450,-230],[-450,-20],[-450,210],[-450,390]],8],
    ["road",[[-230,-470],[-230,-230],[-230,-20],[-230,210],[-230,450]],9],
    ["road",[[-480,-230],[-230,-230],[-30,-230]],9],
    ["road",[[-480,-20],[-230,-20],[-90,-20],[-10,-34]],9],
    ["road",[[-480,210],[-230,210],[-210,210]],8],
    ["road",[[-30,-180],[-80,-80],[-65,100],[-65,280]],7],
    ["road",[[-205,30],[-65,30]],6],
    ["road",[[-145,285],[-145,390],[-65,390]],6],
    ["road",[[-50,280],[10,280],[70,315],[100,325]],8],
    ["road",[[18,-27],[50,-40],[70,-90]],7],
    ["road",[[85,-131],[130,-180],[150,-280]],7],
    ["road",[[94,-272],[155,-300],[200,-340],[270,-334],[360,-350],[430,-350]],7],
    ["road",[[-40,-470],[-70,-460],[-130,-455]],6],
    ["road",[[-470,330],[-460,250],[-450,210]],6],
    ["road",[[-450,-340],[-230,-340]],6],
    ["road",[[-450,-235],[-230,-235]],6],
    ["road",[[-450,-125],[-230,-125]],6],
    ["road",[[-450,85],[-230,85]],6],
    ["road",[[-450,180],[-230,180]],6],
    ["road",[[-230,365],[-195,365]],5],
    ["road",[[-340,205],[-340,260]],3.5]
];

PCG_VEGETATION = [
    ["ruderal",[-480,-480,480,480],260,30,[0.8,1.3],"shrub"]
];

PCG_LANDCOVER = [
    ["nw_birch_oak","hokkaido_mixed",[[-492,240],[-492,492],[-260,492],[-260,340],[-295,340],[-300,250]]],
    ["north_spruce_pocket","spruce",[[-260,420],[-260,492],[-205,492],[-205,430]]],
    ["west_slope_woods","hokkaido_mixed",[[-492,20],[-492,235],[-455,230],[-455,15]]],
    ["shrine_grove","hokkaido_mixed",[[-390,235],[-290,235],[-285,350],[-395,350]]],
    ["north_canal_park","meadow",[[-255,270],[-205,270],[-205,420],[-255,420]]],
    ["north_rail_verge","coastal_grass",[[-225,420],[-90,420],[-90,492],[-225,492]]],
    ["west_back_gardens","meadow",[[-455,-290],[-245,-290],[-245,-185],[-455,-185]]],
    ["middle_back_gardens","meadow",[[-455,-70],[-245,-70],[-245,20],[-455,20]]],
    ["south_rail_verge","coastal_grass",[[-235,-492],[-210,-492],[-210,-250],[-235,-250]]],
    ["quay_reclaimed_grass","coastal_grass",[[100,240],[360,230],[390,290],[310,410],[100,410]]],
    ["south_dock_grass","meadow",[[175,-320],[300,-260],[470,-190],[492,-210],[492,-330],[370,-330]]],
    ["canal_bank_scrub","riparian",[[-100,-470],[165,-470],[165,-160],[90,30],[-80,70]]]
];

PCG_ALLEYS = [
    [[[-450,-440],[-450,195]],"birch",22,6],
    [[[-230,-440],[-230,240]],"elm",24,6],
    [[[-220,270],[-220,410]],"elm",16,5],
    [[[105,235],[350,240]],"birch",25,5],
    [[[180,-310],[430,-350]],"birch",24,6]
];

PCG_CLEAR = [
    [[-15,-35],[35,-23],12],
    [[-85,281],[-45,280],12],
    [[65,-91],[87,-135],12],
    [[-230,280],[-50,280],10],
    [[80,325],[270,325],10]
];
