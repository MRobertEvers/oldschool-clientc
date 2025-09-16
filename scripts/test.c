#include <limits.h>
#include <stdio.h>

static int arrVertexX[768] = {
    311,  1876, 2016, 1348, 866,  908,  618,  1807, 403,  1746, 260,  38,   334,  877,  1149, 835,
    563,  1560, 868,  2040, 584,  1642, 392,  1207, 283,  1502, 2029, 342,  386,  1743, 1039, 64,
    190,  1700, 1255, 777,  1073, 1391, 1493, 1725, 1251, 1388, 873,  980,  1447, 1716, 1502, 493,
    1696, 270,  1855, 1859, 607,  655,  701,  1354, 1416, 910,  310,  1284, 372,  1192, 550,  741,
    1936, 1367, 950,  1779, 25,   121,  333,  115,  730,  1877, 1100, 1800, 789,  1692, 1887, 465,
    1477, 1478, 1400, 647,  771,  684,  319,  609,  1271, 914,  161,  1076, 1037, 1863, 173,  349,
    496,  1976, 1892, 1275, 795,  1087, 2017, 622,  286,  1194, 961,  1723, 1547, 350,  1692, 1366,
    1380, 1692, 622,  783,  1429, 2023, 504,  1968, 1526, 171,  1662, 1060, 374,  610,  564,  1583,
    1979, 1575, 1923, 804,  1598, 47,   1099, 391,  2011, 260,  1170, 286,  1739, 1886, 1528, 860,
    1040, 362,  1446, 1667, 541,  725,  187,  61,   629,  1693, 483,  1274, 267,  707,  459,  1125,
    1107, 1326, 1790, 1245, 70,   1091, 1294, 1655, 1331, 228,  593,  1859, 96,   1715, 161,  1401,
    1748, 1387, 112,  234,  373,  1734, 1240, 1495, 767,  1959, 294,  1278, 1660, 496,  1644, 361,
    1434, 748,  1481, 518,  1078, 1518, 1189, 2000, 780,  1920, 1311, 1772, 1535, 469,  1688, 57,
    1224, 820,  86,   1621, 717,  1121, 864,  1649, 842,  392,  1510, 1450, 1863, 1702, 24,   2040,
    715,  82,   1498, 138,  2006, 1321, 1445, 282,  1492, 546,  702,  998,  512,  519,  1971, 1604,
    2009, 1576, 1329, 679,  710,  975,  901,  1111, 1498, 916,  570,  1933, 1450, 1142, 1508, 362,
    576,  1907, 863,  662,  1499, 1240, 91,   544,  1246, 1563, 818,  5,    554,  1263, 203,  1007,
    1554, 1908, 1522, 2032, 1157, 1854, 957,  1341, 1748, 1130, 670,  128,  1862, 583,  168,  1358,
    1270, 616,  1536, 209,  79,   13,   293,  511,  138,  254,  1460, 1926, 813,  1310, 257,  1502,
    187,  1788, 613,  701,  778,  2035, 1483, 1742, 792,  2009, 1981, 1714, 1897, 536,  708,  1307,
    738,  1196, 414,  374,  1707, 929,  816,  749,  1734, 1741, 330,  980,  1914, 3,    802,  392,
    1129, 1777, 1027, 30,   2001, 1273, 1325, 1643, 1048, 1599, 1407, 1113, 1020, 341,  1965, 1148,
    150,  339,  830,  891,  275,  252,  1056, 1018, 1381, 1896, 1878, 117,  1768, 1790, 1307, 1015,
    1734, 1949, 1153, 1870, 505,  1232, 400,  122,  899,  1753, 244,  1743, 1355, 1082, 1293, 1368,
    1524, 154,  1882, 1997, 564,  159,  1642, 547,  1706, 1706, 1587, 1424, 1191, 100,  13,   1228,
    656,  229,  940,  1477, 475,  1647, 214,  879,  1589, 409,  1764, 878,  1539, 665,  1022, 1321,
    1121, 545,  1160, 967,  793,  400,  249,  737,  70,   661,  1597, 424,  337,  1760, 711,  75,
    1537, 715,  411,  905,  1382, 253,  737,  385,  1126, 363,  894,  772,  359,  773,  1253, 108,
    2041, 613,  1720, 718,  1583, 978,  838,  1841, 1799, 751,  601,  1441, 1576, 122,  1892, 1966,
    235,  277,  2027, 1639, 1065, 572,  429,  204,  1798, 1667, 589,  1491, 1067, 1133, 675,  2007,
    616,  1799, 165,  1001, 1737, 201,  1816, 618,  1895, 1840, 809,  1312, 464,  1769, 877,  336,
    823,  423,  1869, 965,  1742, 118,  1760, 1318, 592,  943,  798,  509,  283,  377,  389,  1192,
    1523, 1824, 1353, 1995, 572,  1244, 661,  1076, 1656, 432,  1242, 1406, 892,  1725, 573,  1212,
    1521, 849,  982,  806,  1743, 292,  214,  1777, 17,   964,  1402, 128,  1626, 1385, 1758, 258,
    85,   744,  503,  422,  48,   766,  1865, 230,  921,  621,  488,  1633, 373,  1596, 1302, 1927,
    133,  1132, 714,  1104, 1544, 45,   978,  852,  1786, 1746, 376,  290,  1335, 654,  1123, 1192,
    1178, 2007, 1724, 1181, 533,  777,  813,  1972, 1805, 789,  1947, 1776, 250,  1832, 1606, 1805,
    141,  396,  501,  713,  1350, 1385, 1750, 2000, 1930, 372,  431,  528,  858,  1635, 1282, 321,
    1467, 199,  403,  1647, 695,  833,  1468, 1053, 1386, 1756, 1789, 302,  206,  1855, 89,   1176,
    1205, 580,  1122, 784,  940,  1301, 639,  1936, 1707, 1578, 1218, 1553, 556,  1112, 563,  1931,
    665,  789,  1487, 1011, 1448, 1398, 1331, 1587, 915,  613,  1593, 781,  824,  1142, 2039, 92,
    733,  1907, 249,  1052, 46,   1627, 627,  1497, 1817, 1234, 644,  206,  361,  195,  1503, 447,
    1073, 342,  784,  1682, 793,  182,  567,  1359, 1988, 1272, 789,  516,  1601, 14,   1770, 921,
    484,  89,   1911, 1488, 1895, 1465, 1030, 925,  892,  1491, 614,  30,   1762, 716,  94,   1888,
    1456, 687,  116,  1841, 2044, 1707, 860,  211,  1977, 1240, 1066, 1927, 1509, 590,  857,  94,
    1558, 102,  458,  591,  1802, 654,  2020, 1909, 1856, 948,  1997, 1444, 1130, 688,  334,  1920,
    529,  330,  1907, 1307, 1983, 543,  518,  417,  678,  1788, 1008, 1974, 465,  1213, 548,  54,
    1518, 960,  1010, 634,  488,  1605, 696,  1682, 994,  1709, 391,  41,   1027, 281,  370,  829
};
static int arrVertexY[768] = {
    857,  1085, 142,  483,  653,  1137, 844,  657,  87,   166,  1953, 1608, 1211, 1901, 957,  926,
    189,  1978, 1155, 1977, 379,  688,  570,  1311, 1137, 1259, 402,  1351, 1399, 1129, 811,  48,
    427,  1328, 1002, 208,  513,  623,  1162, 1067, 1279, 1919, 123,  1312, 1060, 1734, 2037, 1627,
    2032, 1126, 1556, 861,  1987, 1351, 191,  144,  124,  837,  399,  553,  1252, 474,  718,  1067,
    1095, 163,  1025, 57,   1007, 1902, 1710, 44,   1411, 318,  1152, 576,  1492, 144,  683,  1770,
    754,  944,  2015, 1307, 1701, 391,  836,  1764, 1991, 1161, 1953, 1592, 828,  643,  1881, 243,
    468,  550,  1346, 764,  291,  1734, 1463, 214,  94,   920,  1200, 1088, 252,  251,  1466, 773,
    1582, 33,   1280, 1462, 1052, 1131, 503,  1922, 1163, 873,  117,  750,  510,  257,  1982, 794,
    160,  686,  826,  154,  1715, 1466, 301,  1878, 1288, 111,  1007, 621,  1610, 145,  1515, 1428,
    988,  1384, 347,  303,  1789, 1849, 781,  1181, 14,   1448, 829,  1438, 386,  577,  654,  1725,
    338,  1854, 423,  1573, 1289, 443,  1786, 2017, 362,  1358, 2044, 1020, 1831, 856,  1227, 61,
    1832, 172,  1066, 983,  1515, 1651, 541,  1238, 1952, 2010, 1681, 1855, 1644, 13,   639,  648,
    996,  1848, 1646, 1132, 837,  720,  1171, 1098, 320,  1386, 1784, 1790, 1791, 829,  416,  1724,
    1019, 1770, 1783, 1635, 1334, 1561, 552,  333,  1208, 198,  1071, 1355, 1338, 1550, 321,  314,
    1658, 1626, 1256, 1241, 1673, 987,  1783, 410,  327,  1912, 413,  429,  550,  259,  638,  1407,
    1279, 285,  1848, 1474, 1874, 930,  758,  1368, 1565, 1000, 119,  718,  1677, 107,  1119, 1418,
    1219, 1746, 430,  482,  217,  1966, 1712, 1200, 1050, 531,  1515, 877,  1059, 929,  1920, 1418,
    393,  1894, 5,    528,  1038, 497,  45,   1838, 522,  1259, 1027, 1778, 1389, 1233, 1933, 1622,
    653,  1623, 554,  116,  842,  1338, 456,  134,  327,  1207, 1717, 1382, 1752, 1481, 1435, 1109,
    337,  591,  867,  1180, 651,  1457, 1503, 1149, 1644, 298,  146,  1693, 1205, 1602, 243,  1981,
    285,  1764, 1761, 392,  1401, 741,  1767, 1593, 1270, 1479, 1114, 1333, 163,  447,  314,  1165,
    181,  1406, 1967, 1045, 272,  1154, 745,  430,  951,  156,  1078, 890,  1375, 1112, 549,  424,
    1883, 569,  267,  606,  1290, 1426, 1888, 1408, 1455, 1661, 1095, 383,  1096, 1061, 827,  25,
    1870, 127,  113,  1034, 501,  1181, 1598, 479,  1909, 779,  1258, 690,  1080, 1386, 1215, 118,
    364,  1127, 803,  2025, 1898, 1764, 1713, 258,  280,  789,  1159, 342,  737,  1463, 552,  666,
    815,  1302, 2034, 1023, 1610, 952,  378,  1154, 1120, 1573, 1651, 2006, 810,  1940, 2010, 346,
    1318, 200,  967,  1104, 1739, 1950, 1187, 1312, 172,  1271, 468,  980,  388,  716,  896,  1324,
    1804, 1878, 1726, 1510, 1450, 1738, 1588, 38,   1846, 242,  1656, 1771, 670,  1878, 1818, 226,
    1842, 127,  345,  1520, 106,  99,   1580, 361,  1558, 1076, 1375, 1091, 1061, 758,  591,  1644,
    1756, 223,  813,  60,   1470, 770,  1456, 1282, 731,  189,  704,  1786, 1149, 1524, 1336, 1217,
    979,  755,  513,  625,  222,  1552, 1894, 425,  941,  1959, 317,  12,   380,  924,  1565, 1743,
    1805, 1876, 770,  1399, 678,  179,  444,  1459, 1633, 5,    1516, 1518, 1212, 1150, 1054, 1152,
    1277, 1431, 1683, 1202, 940,  1996, 1412, 1019, 1909, 1549, 725,  83,   447,  232,  379,  712,
    1393, 1460, 756,  1164, 476,  123,  448,  184,  1314, 1563, 1001, 75,   1362, 409,  794,  912,
    470,  316,  468,  1815, 1987, 129,  1101, 1383, 1318, 1290, 1538, 1845, 1383, 271,  272,  1867,
    672,  842,  2037, 1318, 1670, 1318, 53,   1452, 1050, 1222, 1891, 1242, 1345, 1280, 2005, 850,
    171,  1763, 130,  210,  100,  1546, 244,  689,  313,  923,  1109, 1043, 1140, 1679, 488,  1992,
    1202, 298,  795,  430,  894,  482,  1153, 900,  1257, 995,  484,  1226, 1921, 1989, 1459, 678,
    1142, 1167, 426,  1633, 1125, 1482, 1940, 336,  1286, 772,  70,   817,  588,  9,    1777, 367,
    303,  756,  267,  1558, 1230, 755,  1423, 1705, 636,  976,  815,  720,  1818, 1861, 820,  1252,
    1776, 729,  485,  478,  592,  1879, 1396, 152,  1326, 467,  176,  1755, 1263, 379,  516,  159,
    1921, 429,  427,  1318, 1326, 103,  552,  851,  2002, 1488, 1321, 128,  745,  104,  439,  1612,
    1024, 1701, 497,  126,  1539, 861,  108,  956,  1328, 1079, 1520, 179,  1017, 1022, 862,  1955,
    21,   563,  856,  1890, 697,  1165, 1440, 673,  1260, 99,   1831, 1890, 1355, 967,  1024, 860,
    1981, 1224, 1587, 1949, 593,  1076, 1890, 657,  1530, 1666, 554,  108,  1285, 1678, 1498, 1632,
    785,  1430, 1053, 671,  1507, 335,  1015, 294,  298,  587,  340,  1414, 880,  1507, 971,  1603,
    1774, 20,   553,  916,  815,  716,  429,  1638, 1185, 1723, 801,  553,  1839, 581,  892,  745,
    1319, 731,  1713, 1449, 137,  592,  379,  323,  789,  789,  229,  107,  617,  1234, 782,  1728
};
static int arrVertexZ[768] = {
    966,  1261, 1313, 1153, 397,  1310, 1920, 1147, 1535, 552,  672,  351,  1076, 1445, 2029, 1737,
    1039, 1779, 1416, 1999, 736,  1553, 973,  1845, 1854, 114,  908,  1741, 42,   617,  1173, 975,
    1951, 1868, 891,  460,  492,  746,  1779, 1430, 410,  571,  196,  1081, 1294, 242,  729,  347,
    894,  1911, 1348, 1921, 848,  316,  1495, 240,  1786, 668,  2032, 1043, 261,  1951, 992,  374,
    476,  1247, 1413, 1980, 1567, 191,  1317, 629,  1848, 1589, 1477, 675,  787,  1140, 53,   251,
    1815, 1994, 510,  467,  765,  512,  708,  1600, 250,  974,  358,  1229, 140,  57,   390,  1813,
    1686, 1245, 1284, 1648, 825,  461,  1667, 899,  1441, 50,   842,  416,  1056, 207,  244,  811,
    1676, 1574, 1063, 1450, 1750, 399,  394,  0,    1545, 468,  29,   605,  1408, 1022, 1246, 1315,
    464,  78,   1878, 370,  1784, 476,  873,  1761, 261,  81,   185,  716,  2008, 1503, 1213, 1888,
    1760, 51,   1813, 631,  820,  716,  3,    1344, 12,   499,  1929, 78,   1902, 1642, 120,  1517,
    39,   889,  770,  270,  235,  532,  1943, 2011, 1454, 2032, 1581, 829,  981,  892,  944,  342,
    528,  719,  152,  1480, 1500, 473,  1435, 205,  1570, 2028, 1743, 302,  1723, 473,  462,  942,
    1802, 130,  298,  1620, 192,  1772, 1221, 1251, 1428, 1661, 1594, 1088, 654,  1003, 823,  1137,
    898,  1866, 1991, 1003, 581,  261,  1785, 2041, 872,  1719, 711,  872,  1879, 1552, 558,  1732,
    1418, 1455, 1531, 676,  1999, 1724, 370,  650,  825,  474,  701,  1896, 1743, 1125, 498,  160,
    1794, 375,  1832, 53,   139,  1252, 1324, 1961, 522,  351,  181,  1537, 1015, 1432, 1359, 1445,
    340,  1124, 585,  1011, 130,  394,  210,  241,  1746, 615,  1733, 1086, 1815, 1490, 1683, 1125,
    181,  1794, 943,  1400, 599,  1041, 1942, 751,  1524, 1783, 163,  1228, 560,  1844, 262,  706,
    1649, 1874, 467,  21,   863,  1878, 632,  334,  129,  1873, 1515, 1726, 1247, 1411, 1570, 343,
    403,  1121, 590,  644,  717,  1799, 617,  801,  120,  1767, 744,  1993, 972,  1457, 545,  324,
    981,  1917, 1400, 1192, 462,  1758, 288,  1001, 1528, 146,  778,  857,  1639, 478,  1629, 1011,
    1719, 969,  722,  1430, 1358, 343,  413,  628,  1544, 767,  1464, 1694, 1930, 1328, 1859, 1490,
    1660, 1765, 793,  1049, 49,   890,  299,  624,  1402, 629,  229,  843,  2018, 1041, 1742, 934,
    1717, 653,  1148, 838,  1107, 1697, 622,  357,  931,  1292, 640,  1019, 597,  14,   972,  393,
    672,  1572, 1162, 1769, 622,  754,  862,  64,   1253, 594,  1826, 173,  461,  741,  837,  1381,
    1857, 1558, 1619, 1306, 192,  99,   1660, 1341, 680,  942,  80,   770,  634,  1133, 223,  233,
    102,  1645, 1734, 1374, 396,  1211, 597,  724,  1081, 137,  1391, 433,  1168, 1126, 1395, 2026,
    296,  1378, 1559, 1114, 678,  902,  1133, 112,  667,  1938, 1913, 1343, 419,  456,  1644, 445,
    1482, 1592, 209,  929,  1535, 1507, 1693, 183,  792,  1246, 91,   2004, 1589, 932,  1356, 96,
    1159, 2003, 909,  473,  1839, 1180, 1777, 626,  547,  775,  972,  1697, 1173, 421,  5,    446,
    736,  1660, 106,  2002, 191,  354,  713,  1372, 594,  1599, 2034, 1800, 193,  895,  852,  320,
    396,  161,  351,  170,  1179, 1128, 1443, 1425, 1610, 199,  1643, 1989, 1839, 1481, 605,  1909,
    1266, 585,  597,  780,  1362, 399,  1326, 1383, 9,    451,  914,  1364, 692,  1921, 353,  865,
    1678, 274,  693,  188,  1037, 894,  410,  1705, 671,  469,  425,  1257, 1390, 340,  905,  286,
    1438, 1429, 938,  297,  644,  1846, 1242, 1721, 980,  1698, 1087, 1418, 827,  1066, 1083, 1894,
    1267, 1631, 443,  960,  347,  1498, 647,  1916, 1667, 1190, 227,  356,  1572, 344,  143,  1501,
    2028, 1271, 100,  587,  1650, 124,  1301, 1225, 1493, 851,  569,  2010, 1530, 1722, 1566, 399,
    494,  1503, 35,   280,  898,  445,  1297, 1204, 1164, 1476, 1438, 500,  1050, 1647, 1787, 1374,
    863,  1005, 1463, 886,  2035, 1215, 513,  850,  1003, 61,   754,  1661, 1371, 871,  511,  1143,
    1489, 1101, 240,  520,  1747, 1190, 642,  406,  163,  406,  600,  1825, 1880, 817,  926,  2031,
    430,  2045, 693,  1131, 1786, 1283, 462,  257,  621,  1098, 1810, 1919, 1122, 1376, 913,  1259,
    1651, 1987, 1643, 936,  475,  999,  1619, 611,  38,   1696, 647,  453,  1952, 255,  1971, 444,
    1323, 1919, 853,  1437, 13,   1936, 32,   1766, 1236, 1374, 969,  173,  242,  1085, 473,  1973,
    860,  189,  919,  386,  981,  232,  1102, 694,  35,   837,  892,  1206, 1311, 427,  1343, 469,
    1990, 2028, 625,  905,  1498, 1748, 1643, 1766, 1496, 1062, 1740, 1866, 1383, 697,  1594, 1084,
    1074, 1823, 1787, 1770, 178,  429,  937,  1420, 918,  978,  473,  2024, 1421, 551,  521,  252,
    42,   1167, 1736, 1853, 1756, 422,  595,  696,  833,  1374, 1000, 1024, 988,  766,  566,  1845,
    397,  1365, 6,    360,  297,  1155, 67,   446,  423,  98,   1123, 1600, 473,  1638, 1183, 472
};
static int arrFaceIndicesA[128] = {
    411, 50,  595, 667, 68,  537, 766, 134, 168, 111, 89,  486, 327, 491, 487, 59,  536, 427, 245,
    697, 332, 694, 646, 537, 146, 62,  648, 727, 152, 354, 517, 331, 57,  618, 649, 331, 61,  657,
    264, 7,   767, 516, 272, 511, 279, 180, 231, 727, 111, 343, 233, 149, 222, 673, 156, 135, 640,
    11,  338, 19,  29,  68,  101, 755, 576, 649, 386, 109, 582, 74,  627, 181, 154, 597, 270, 2,
    317, 588, 620, 597, 506, 340, 519, 644, 660, 521, 709, 488, 147, 370, 161, 440, 615, 538, 484,
    182, 703, 255, 66,  367, 279, 616, 470, 530, 578, 30,  672, 119, 390, 572, 652, 756, 687, 220,
    575, 360, 590, 662, 340, 413, 727, 31,  684, 8,   573, 394, 742, 617
};
static int arrFaceIndicesB[128] = {
    44,  194, 322, 759, 311, 384, 356, 718, 102, 201, 695, 721, 703, 685, 663, 501, 154, 316, 677,
    613, 483, 476, 373, 454, 661, 686, 228, 134, 82,  757, 16,  758, 512, 593, 438, 729, 515, 593,
    646, 636, 331, 179, 552, 605, 473, 482, 85,  114, 528, 465, 554, 103, 234, 477, 64,  28,  241,
    607, 242, 600, 129, 397, 146, 398, 482, 617, 194, 651, 336, 117, 259, 360, 251, 651, 171, 120,
    126, 657, 69,  159, 275, 618, 55,  325, 686, 119, 442, 10,  62,  546, 684, 432, 440, 293, 762,
    537, 267, 156, 283, 553, 212, 105, 229, 555, 622, 263, 681, 485, 152, 257, 240, 520, 630, 271,
    294, 105, 89,  630, 394, 312, 73,  240, 106, 433, 725, 498, 497, 663
};
static int arrFaceIndicesC[128] = {
    576, 144, 493, 599, 42,  612, 323, 290, 122, 429, 327, 248, 714, 409, 317, 36,  434, 436, 69,
    657, 458, 214, 570, 446, 509, 407, 453, 76,  299, 708, 604, 431, 253, 390, 664, 406, 519, 127,
    3,   329, 495, 503, 499, 116, 549, 593, 5,   497, 741, 416, 300, 447, 101, 502, 33,  232, 398,
    258, 500, 285, 129, 67,  508, 189, 525, 557, 636, 386, 339, 476, 58,  151, 741, 11,  522, 674,
    174, 685, 723, 330, 633, 101, 638, 206, 311, 713, 458, 77,  55,  606, 490, 496, 672, 59,  88,
    515, 72,  493, 510, 196, 726, 631, 476, 627, 589, 684, 590, 591, 422, 60,  91,  662, 753, 553,
    726, 208, 118, 58,  428, 355, 488, 664, 634, 431, 695, 62,  248, 700
};

static inline int
bucket_sort_by_average_depth_baseline(
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    int model_min_depth,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* face_a,
    int* face_b,
    int* face_c)
{
    int min_depth = INT_MAX;
    int max_depth = INT_MIN;

    for( int f = 0; f < num_faces; f++ )
    {
        int a = face_a[f];
        int b = face_b[f];
        int c = face_c[f];

        int xa = vertex_x[a];
        int xb = vertex_x[b];
        int xc = vertex_x[c];

        int ya = vertex_y[a];
        int yb = vertex_y[b];
        int yc = vertex_y[c];

        int za = vertex_z[a];
        int zb = vertex_z[b];
        int zc = vertex_z[c];

        // dot product of the vectors (AB, BC)
        // If the dot product is 0, then AB and BC are on the same line.
        // if the dot product is negative, then the triangle is facing away from the camera.
        // Note: This assumes that face vertices are ordered in some way. TODO: Determine that
        // order.
        int dot_product = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
        if( dot_product > 0 )
        {
            // the z's are the depth of the vertex relative to the origin of the model.
            // This means some of the z's will be negative.
            // model_min_depth is calculate from as the radius sphere around the origin of the
            // model, that encloses all vertices on the model. This adjusts all the z's to be
            // positive, but maintain relative order.
            //
            //
            // Note: In osrs, the min_depth is actually calculated from a cylinder that encloses
            // the model
            //
            //                   / |
            //  min_depth ->   /    |
            //               /      |
            //              /       |
            //              --------
            //              ^ model xz radius
            //    Note: There is a lower cylinder as well, but it is not used in depth sorting.
            // The reason is uses the models "upper ys" (max_y) is because OSRS assumes the
            // camera will always be above the model, so the closest vertices to the camera will
            // be towards the top of the model. (i.e. lowest z values) Relative to the model's
            // origin, there may be negative z values, but always |z| < min_depth so the
            // min_depth is used to adjust all z values to be positive, but maintain relative
            // order.
            int depth_average = ((za + zb + zc) / 3) + model_min_depth;

            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[(depth_average << 9) + bucket_index] = f;

                if( depth_average < min_depth )
                    min_depth = depth_average;
                if( depth_average > max_depth )
                    max_depth = depth_average;
            }
        }
    }

    return (min_depth) | (max_depth << 16);
}

static inline int
bucket_sort_by_average_depth_shift(
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    int model_min_depth,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* face_a,
    int* face_b,
    int* face_c)
{
    int min_depth = INT_MAX;
    int max_depth = INT_MIN;

    for( int f = 0; f < num_faces; f++ )
    {
        int a = face_a[f];
        int b = face_b[f];
        int c = face_c[f];

        int xa = vertex_x[a];
        int xb = vertex_x[b];
        int xc = vertex_x[c];

        int ya = vertex_y[a];
        int yb = vertex_y[b];
        int yc = vertex_y[c];

        int za = vertex_z[a];
        int zb = vertex_z[b];
        int zc = vertex_z[c];

        // dot product of the vectors (AB, BC)
        // If the dot product is 0, then AB and BC are on the same line.
        // if the dot product is negative, then the triangle is facing away from the camera.
        // Note: This assumes that face vertices are ordered in some way. TODO: Determine that
        // order.
        int dot_product = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
        if( dot_product > 0 )
        {
            // the z's are the depth of the vertex relative to the origin of the model.
            // This means some of the z's will be negative.
            // model_min_depth is calculate from as the radius sphere around the origin of the
            // model, that encloses all vertices on the model. This adjusts all the z's to be
            // positive, but maintain relative order.
            //
            //
            // Note: In osrs, the min_depth is actually calculated from a cylinder that encloses
            // the model
            //
            //                   / |
            //  min_depth ->   /    |
            //               /      |
            //              /       |
            //              --------
            //              ^ model xz radius
            //    Note: There is a lower cylinder as well, but it is not used in depth sorting.
            // The reason is uses the models "upper ys" (max_y) is because OSRS assumes the
            // camera will always be above the model, so the closest vertices to the camera will
            // be towards the top of the model. (i.e. lowest z values) Relative to the model's
            // origin, there may be negative z values, but always |z| < min_depth so the
            // min_depth is used to adjust all z values to be positive, but maintain relative
            // order.
            int depth_average = ((za + zb + zc) >> 2) + model_min_depth;

            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[(depth_average << 9) + bucket_index] = f;

                if( depth_average < min_depth )
                    min_depth = depth_average;
                if( depth_average > max_depth )
                    max_depth = depth_average;
            }
        }
    }

    return (min_depth) | (max_depth << 16);
}
// AArch64 / ARMv8 NEON
// Compile with: -O3 -mcpu=native (or -march=armv8-a+simd) and -ffast-math if acceptable
#include <arm_neon.h>

#include <limits.h>

static inline int
bucket_sort_by_average_depth_vector(
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    int model_min_depth,
    int num_faces,
    const int* __restrict vertex_x,
    const int* __restrict vertex_y,
    const int* __restrict vertex_z,
    const int* __restrict face_a,
    const int* __restrict face_b,
    const int* __restrict face_c)
{
    int min_depth_scalar = INT_MAX;
    int max_depth_scalar = INT_MIN;

    // Vector constants
    const int32x4_t v_zero = vdupq_n_s32(0);
    const int32x4_t v_1500 = vdupq_n_s32(1500);
    const int32x4_t v_min_off = vdupq_n_s32(model_min_depth);

    int f = 0;
    for( ; f + 3 < num_faces; f += 4 )
    {
        // --- scalar gather of vertex indices (no generic NEON gather) ---
        int a0 = face_a[f + 0], a1 = face_a[f + 1], a2 = face_a[f + 2], a3 = face_a[f + 3];
        int b0 = face_b[f + 0], b1 = face_b[f + 1], b2 = face_b[f + 2], b3 = face_b[f + 3];
        int c0 = face_c[f + 0], c1 = face_c[f + 1], c2 = face_c[f + 2], c3 = face_c[f + 3];

        int xa0 = vertex_x[a0], xa1 = vertex_x[a1], xa2 = vertex_x[a2], xa3 = vertex_x[a3];
        int xb0 = vertex_x[b0], xb1 = vertex_x[b1], xb2 = vertex_x[b2], xb3 = vertex_x[b3];
        int xc0 = vertex_x[c0], xc1 = vertex_x[c1], xc2 = vertex_x[c2], xc3 = vertex_x[c3];

        int ya0 = vertex_y[a0], ya1 = vertex_y[a1], ya2 = vertex_y[a2], ya3 = vertex_y[a3];
        int yb0 = vertex_y[b0], yb1 = vertex_y[b1], yb2 = vertex_y[b2], yb3 = vertex_y[b3];
        int yc0 = vertex_y[c0], yc1 = vertex_y[c1], yc2 = vertex_y[c2], yc3 = vertex_y[c3];

        int za0 = vertex_z[a0], za1 = vertex_z[a1], za2 = vertex_z[a2], za3 = vertex_z[a3];
        int zb0 = vertex_z[b0], zb1 = vertex_z[b1], zb2 = vertex_z[b2], zb3 = vertex_z[b3];
        int zc0 = vertex_z[c0], zc1 = vertex_z[c1], zc2 = vertex_z[c2], zc3 = vertex_z[c3];

        // --- pack into vectors ---
        int32x4_t xa = { xa0, xa1, xa2, xa3 };
        int32x4_t xb = { xb0, xb1, xb2, xb3 };
        int32x4_t xc = { xc0, xc1, xc2, xc3 };

        int32x4_t ya = { ya0, ya1, ya2, ya3 };
        int32x4_t yb = { yb0, yb1, yb2, yb3 };
        int32x4_t yc = { yc0, yc1, yc2, yc3 };

        int32x4_t za = { za0, za1, za2, za3 };
        int32x4_t zb = { zb0, zb1, zb2, zb3 };
        int32x4_t zc = { zc0, zc1, zc2, zc3 };

        // --- dot product sign test: (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb) > 0 ---
        int32x4_t abx = vsubq_s32(xa, xb);
        int32x4_t bcy = vsubq_s32(yc, yb);

        int32x4_t aby = vsubq_s32(ya, yb);
        int32x4_t bcx = vsubq_s32(xc, xb);

        // muls
        int32x4_t m1 = vmulq_s32(abx, bcy);
        int32x4_t m2 = vmulq_s32(aby, bcx);
        int32x4_t dot = vsubq_s32(m1, m2);

        // mask: dot > 0
        uint32x4_t pass_facing = vcgtq_s32(dot, v_zero);

        // --- average depth: ((za + zb + zc) >> 2) + model_min_depth ---
        int32x4_t zsum = vaddq_s32(vaddq_s32(za, zb), zc);
        // arithmetic shift right by 2
        int32x4_t zavg = vshrq_n_s32(zsum, 2);
        int32x4_t depth = vaddq_s32(zavg, v_min_off);

        // masks for depth in (0,1500)
        uint32x4_t gt0 = vcgtq_s32(depth, v_zero);
        uint32x4_t lt1500 = vcltq_s32(depth, v_1500);
        uint32x4_t pass_depth = vandq_u32(gt0, lt1500);

        // combined mask
        uint32x4_t pass_all = vandq_u32(pass_facing, pass_depth);

        // Update min/max with masked depth:
        // Use "select": if not pass, keep previous extremum by feeding neutral values via lane-wise
        // extraction We'll do lane-wise updates (cheap) to avoid finite-min/max select trickery.
        int depths[4];
        vst1q_s32(depths, depth);

        uint32_t mask_bits[4];
        vst1q_u32(mask_bits, pass_all);

        // Scalarize per accepted lane for buckets + min/max
        if( mask_bits[0] )
        {
            int d = depths[0];
            int idx = face_depth_bucket_counts[d]++;             // increment count
            face_depth_buckets[(d << 9) + idx] = (short)(f + 0); // store face index
            if( d < min_depth_scalar )
                min_depth_scalar = d;
            if( d > max_depth_scalar )
                max_depth_scalar = d;
        }
        if( mask_bits[1] )
        {
            int d = depths[1];
            int idx = face_depth_bucket_counts[d]++;
            face_depth_buckets[(d << 9) + idx] = (short)(f + 1);
            if( d < min_depth_scalar )
                min_depth_scalar = d;
            if( d > max_depth_scalar )
                max_depth_scalar = d;
        }
        if( mask_bits[2] )
        {
            int d = depths[2];
            int idx = face_depth_bucket_counts[d]++;
            face_depth_buckets[(d << 9) + idx] = (short)(f + 2);
            if( d < min_depth_scalar )
                min_depth_scalar = d;
            if( d > max_depth_scalar )
                max_depth_scalar = d;
        }
        if( mask_bits[3] )
        {
            int d = depths[3];
            int idx = face_depth_bucket_counts[d]++;
            face_depth_buckets[(d << 9) + idx] = (short)(f + 3);
            if( d < min_depth_scalar )
                min_depth_scalar = d;
            if( d > max_depth_scalar )
                max_depth_scalar = d;
        }
    }

    // --- tail (0..3 remaining faces), original scalar logic ---
    for( ; f < num_faces; ++f )
    {
        int a = face_a[f];
        int b = face_b[f];
        int c = face_c[f];

        int xa = vertex_x[a], xb = vertex_x[b], xc = vertex_x[c];
        int ya = vertex_y[a], yb = vertex_y[b], yc = vertex_y[c];
        int za = vertex_z[a], zb = vertex_z[b], zc = vertex_z[c];

        int dot_product = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
        if( dot_product > 0 )
        {
            int depth_average = ((za + zb + zc) >> 2) + model_min_depth;
            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[(depth_average << 9) + bucket_index] = (short)f;

                if( depth_average < min_depth_scalar )
                    min_depth_scalar = depth_average;
                if( depth_average > max_depth_scalar )
                    max_depth_scalar = depth_average;
            }
        }
    }

    return (min_depth_scalar) | (max_depth_scalar << 16);
}

static short arrFaceDepthBuckets[1500 * 512] = { 0 };
static short arrFaceDepthBucketCounts[1500] = { 0 };
static int arrModelMinDepth = 7;
static int arrFaceCount = 128;
#include <mach/mach_time.h>

// Baseline division by 3
// === RESULTS ===
// Successful runs: 1000/1000
// Average: 132990.29 ns
// Minimum: 130083 ns
// Maximum: 173625 ns
// Range: 43542 ns
// Standard deviation: 7046.13 ns

// shift by 2
// === RESULTS ===
// Successful runs: 1000/1000
// Average: 132600.38 ns
// Minimum: 130125 ns
// Maximum: 177666 ns
// Range: 47541 ns
// Standard deviation: 6328.80 ns
int
main()
{
    uint64_t start = mach_absolute_time();
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);

    for( int test = 0; test < 1000; test++ )
    {
        bucket_sort_by_average_depth_vector(
            arrFaceDepthBuckets,
            arrFaceDepthBucketCounts,
            arrModelMinDepth,
            arrFaceCount,
            arrVertexX,
            arrVertexY,
            arrVertexZ,
            arrFaceIndicesA,
            arrFaceIndicesB,
            arrFaceIndicesC);
    }

    uint64_t end = mach_absolute_time();
    uint64_t elapsed = end - start;
    uint64_t nanoseconds = elapsed * timebase.numer / timebase.denom;
    printf("Time elapsed: %llu ns\n", nanoseconds);
}