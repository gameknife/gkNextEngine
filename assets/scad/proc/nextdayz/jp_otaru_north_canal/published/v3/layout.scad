// Otaru urban gameplay controls. Geography is reference, not a building list.
// 120 buildings; connected residential streets, canal warehouses and freight yards.
// Site=[id,name,kind,center,padSize,rotation,buildingCount,risk].

PCG_SITES = [
    ["south_housing","South residential blocks","urban",[-340,-340],[200,160],0,24,1],
    ["central_housing","Iro-nai residential streets","urban",[-340,-130],[200,160],0,24,1],
    ["hill_housing","Hillside neighbourhood","urban",[-340,95],[200,160],-8,20,1],
    ["market_street","Ransacked shopping streets","downtown",[-115,-180],[150,170],0,16,2],
    ["canal_warehouses","Canal stone warehouses","port",[-145,210],[120,150],0,8,2],
    ["north_quay","North cargo quay","port",[175,325],[150,130],0,8,2],
    ["southern_freight","South freight workshops","industry",[270,-390],[130,100],0,6,2],
    ["clinic","Emergency clinic","medical",[-130,30],[130,96],0,4,2],
    ["police","South police cordon","checkpoint",[-130,-410],[120,80],0,4,2],
    ["port_command","South quarantine command","military",[-130,-317],[100,76],0,6,3]
];

PCG_SPAWNS = [
    [-470,-470],
    [-470,330],
    [-160,455],
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
    ["road",[[-470,330],[-460,250],[-450,210]],6]
];

PCG_VEGETATION = [
    ["hillside_green",[-480,230,-230,480],180,30,[1.4,2.3],"tree"],
    ["fringe_trees",[-480,-480,-430,210],60,28,[1.2,1.9],"tree"],
    ["canal_greens",[-220,-480,150,480],65,25,[1.2,1.8],"tree"],
    ["reclaimed_lots",[-480,-480,480,480],380,30,[0.8,1.5],"shrub"],
    ["dead_street_trees",[-480,-480,460,470],40,28,[1.1,1.7],"dead"]
];

PCG_CLEAR = [
    [[-15,-35],[35,-23],12],
    [[-85,281],[-45,280],12],
    [[65,-91],[87,-135],12]
];
