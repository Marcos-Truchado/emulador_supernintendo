#include "coprocessor/dsp1.hpp"
#include <cstring>
#include <cstdint>
#include <algorithm>
namespace snes {
static inline uint16 rd16(const uint8* p){ return uint16(p[0]) | (uint16(p[1])<<8); }
static inline void wr16(uint8* p, uint16 v){ p[0]=uint8(v); p[1]=uint8(v>>8); }
static inline int16 rd16s(const uint8* p){ return int16(rd16(p)); }
const uint16 Dsp1::kRom[1024] = {
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0001,  0x0002,  0x0004,  0x0008,  0x0010,  0x0020,
	 0x0040,  0x0080,  0x0100,  0x0200,  0x0400,  0x0800,  0x1000,  0x2000,
	 0x4000,  0x7fff,  0x4000,  0x2000,  0x1000,  0x0800,  0x0400,  0x0200,
	 0x0100,  0x0080,  0x0040,  0x0020,  0x0001,  0x0008,  0x0004,  0x0002,
	 0x0001,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,
	 0x0000,  0x0000,  0x8000,  0xffe5,  0x0100,  0x7fff,  0x7f02,  0x7e08,
	 0x7d12,  0x7c1f,  0x7b30,  0x7a45,  0x795d,  0x7878,  0x7797,  0x76ba,
	 0x75df,  0x7507,  0x7433,  0x7361,  0x7293,  0x71c7,  0x70fe,  0x7038,
	 0x6f75,  0x6eb4,  0x6df6,  0x6d3a,  0x6c81,  0x6bca,  0x6b16,  0x6a64,
	 0x69b4,  0x6907,  0x685b,  0x67b2,  0x670b,  0x6666,  0x65c4,  0x6523,
	 0x6484,  0x63e7,  0x634c,  0x62b3,  0x621c,  0x6186,  0x60f2,  0x6060,
	 0x5fd0,  0x5f41,  0x5eb5,  0x5e29,  0x5d9f,  0x5d17,  0x5c91,  0x5c0c,
	 0x5b88,  0x5b06,  0x5a85,  0x5a06,  0x5988,  0x590b,  0x5890,  0x5816,
	 0x579d,  0x5726,  0x56b0,  0x563b,  0x55c8,  0x5555,  0x54e4,  0x5474,
	 0x5405,  0x5398,  0x532b,  0x52bf,  0x5255,  0x51ec,  0x5183,  0x511c,
	 0x50b6,  0x5050,  0x4fec,  0x4f89,  0x4f26,  0x4ec5,  0x4e64,  0x4e05,
	 0x4da6,  0x4d48,  0x4cec,  0x4c90,  0x4c34,  0x4bda,  0x4b81,  0x4b28,
	 0x4ad0,  0x4a79,  0x4a23,  0x49cd,  0x4979,  0x4925,  0x48d1,  0x487f,
	 0x482d,  0x47dc,  0x478c,  0x473c,  0x46ed,  0x469f,  0x4651,  0x4604,
	 0x45b8,  0x456c,  0x4521,  0x44d7,  0x448d,  0x4444,  0x43fc,  0x43b4,
	 0x436d,  0x4326,  0x42e0,  0x429a,  0x4255,  0x4211,  0x41cd,  0x4189,
	 0x4146,  0x4104,  0x40c2,  0x4081,  0x4040,  0x3fff,  0x41f7,  0x43e1,
	 0x45bd,  0x478d,  0x4951,  0x4b0b,  0x4cbb,  0x4e61,  0x4fff,  0x5194,
	 0x5322,  0x54a9,  0x5628,  0x57a2,  0x5914,  0x5a81,  0x5be9,  0x5d4a,
	 0x5ea7,  0x5fff,  0x6152,  0x62a0,  0x63ea,  0x6530,  0x6672,  0x67b0,
	 0x68ea,  0x6a20,  0x6b53,  0x6c83,  0x6daf,  0x6ed9,  0x6fff,  0x7122,
	 0x7242,  0x735f,  0x747a,  0x7592,  0x76a7,  0x77ba,  0x78cb,  0x79d9,
	 0x7ae5,  0x7bee,  0x7cf5,  0x7dfa,  0x7efe,  0x7fff,  0x0000,  0x0324,
	 0x0647,  0x096a,  0x0c8b,  0x0fab,  0x12c8,  0x15e2,  0x18f8,  0x1c0b,
	 0x1f19,  0x2223,  0x2528,  0x2826,  0x2b1f,  0x2e11,  0x30fb,  0x33de,
	 0x36ba,  0x398c,  0x3c56,  0x3f17,  0x41ce,  0x447a,  0x471c,  0x49b4,
	 0x4c3f,  0x4ebf,  0x5133,  0x539b,  0x55f5,  0x5842,  0x5a82,  0x5cb4,
	 0x5ed7,  0x60ec,  0x62f2,  0x64e8,  0x66cf,  0x68a6,  0x6a6d,  0x6c24,
	 0x6dca,  0x6f5f,  0x70e2,  0x7255,  0x73b5,  0x7504,  0x7641,  0x776c,
	 0x7884,  0x798a,  0x7a7d,  0x7b5d,  0x7c29,  0x7ce3,  0x7d8a,  0x7e1d,
	 0x7e9d,  0x7f09,  0x7f62,  0x7fa7,  0x7fd8,  0x7ff6,  0x7fff,  0x7ff6,
	 0x7fd8,  0x7fa7,  0x7f62,  0x7f09,  0x7e9d,  0x7e1d,  0x7d8a,  0x7ce3,
	 0x7c29,  0x7b5d,  0x7a7d,  0x798a,  0x7884,  0x776c,  0x7641,  0x7504,
	 0x73b5,  0x7255,  0x70e2,  0x6f5f,  0x6dca,  0x6c24,  0x6a6d,  0x68a6,
	 0x66cf,  0x64e8,  0x62f2,  0x60ec,  0x5ed7,  0x5cb4,  0x5a82,  0x5842,
	 0x55f5,  0x539b,  0x5133,  0x4ebf,  0x4c3f,  0x49b4,  0x471c,  0x447a,
	 0x41ce,  0x3f17,  0x3c56,  0x398c,  0x36ba,  0x33de,  0x30fb,  0x2e11,
	 0x2b1f,  0x2826,  0x2528,  0x2223,  0x1f19,  0x1c0b,  0x18f8,  0x15e2,
	 0x12c8,  0x0fab,  0x0c8b,  0x096a,  0x0647,  0x0324,  0x7fff,  0x7ff6,
	 0x7fd8,  0x7fa7,  0x7f62,  0x7f09,  0x7e9d,  0x7e1d,  0x7d8a,  0x7ce3,
	 0x7c29,  0x7b5d,  0x7a7d,  0x798a,  0x7884,  0x776c,  0x7641,  0x7504,
	 0x73b5,  0x7255,  0x70e2,  0x6f5f,  0x6dca,  0x6c24,  0x6a6d,  0x68a6,
	 0x66cf,  0x64e8,  0x62f2,  0x60ec,  0x5ed7,  0x5cb4,  0x5a82,  0x5842,
	 0x55f5,  0x539b,  0x5133,  0x4ebf,  0x4c3f,  0x49b4,  0x471c,  0x447a,
	 0x41ce,  0x3f17,  0x3c56,  0x398c,  0x36ba,  0x33de,  0x30fb,  0x2e11,
	 0x2b1f,  0x2826,  0x2528,  0x2223,  0x1f19,  0x1c0b,  0x18f8,  0x15e2,
	 0x12c8,  0x0fab,  0x0c8b,  0x096a,  0x0647,  0x0324,  0x0000,  0xfcdc,
	 0xf9b9,  0xf696,  0xf375,  0xf055,  0xed38,  0xea1e,  0xe708,  0xe3f5,
	 0xe0e7,  0xdddd,  0xdad8,  0xd7da,  0xd4e1,  0xd1ef,  0xcf05,  0xcc22,
	 0xc946,  0xc674,  0xc3aa,  0xc0e9,  0xbe32,  0xbb86,  0xb8e4,  0xb64c,
	 0xb3c1,  0xb141,  0xaecd,  0xac65,  0xaa0b,  0xa7be,  0xa57e,  0xa34c,
	 0xa129,  0x9f14,  0x9d0e,  0x9b18,  0x9931,  0x975a,  0x9593,  0x93dc,
	 0x9236,  0x90a1,  0x8f1e,  0x8dab,  0x8c4b,  0x8afc,  0x89bf,  0x8894,
	 0x877c,  0x8676,  0x8583,  0x84a3,  0x83d7,  0x831d,  0x8276,  0x81e3,
	 0x8163,  0x80f7,  0x809e,  0x8059,  0x8028,  0x800a,  0x6488,  0x0080,
	 0x03ff,  0x0116,  0x0002,  0x0080,  0x4000,  0x3fd7,  0x3faf,  0x3f86,
	 0x3f5d,  0x3f34,  0x3f0c,  0x3ee3,  0x3eba,  0x3e91,  0x3e68,  0x3e40,
	 0x3e17,  0x3dee,  0x3dc5,  0x3d9c,  0x3d74,  0x3d4b,  0x3d22,  0x3cf9,
	 0x3cd0,  0x3ca7,  0x3c7f,  0x3c56,  0x3c2d,  0x3c04,  0x3bdb,  0x3bb2,
	 0x3b89,  0x3b60,  0x3b37,  0x3b0e,  0x3ae5,  0x3abc,  0x3a93,  0x3a69,
	 0x3a40,  0x3a17,  0x39ee,  0x39c5,  0x399c,  0x3972,  0x3949,  0x3920,
	 0x38f6,  0x38cd,  0x38a4,  0x387a,  0x3851,  0x3827,  0x37fe,  0x37d4,
	 0x37aa,  0x3781,  0x3757,  0x372d,  0x3704,  0x36da,  0x36b0,  0x3686,
	 0x365c,  0x3632,  0x3609,  0x35df,  0x35b4,  0x358a,  0x3560,  0x3536,
	 0x350c,  0x34e1,  0x34b7,  0x348d,  0x3462,  0x3438,  0x340d,  0x33e3,
	 0x33b8,  0x338d,  0x3363,  0x3338,  0x330d,  0x32e2,  0x32b7,  0x328c,
	 0x3261,  0x3236,  0x320b,  0x31df,  0x31b4,  0x3188,  0x315d,  0x3131,
	 0x3106,  0x30da,  0x30ae,  0x3083,  0x3057,  0x302b,  0x2fff,  0x2fd2,
	 0x2fa6,  0x2f7a,  0x2f4d,  0x2f21,  0x2ef4,  0x2ec8,  0x2e9b,  0x2e6e,
	 0x2e41,  0x2e14,  0x2de7,  0x2dba,  0x2d8d,  0x2d60,  0x2d32,  0x2d05,
	 0x2cd7,  0x2ca9,  0x2c7b,  0x2c4d,  0x2c1f,  0x2bf1,  0x2bc3,  0x2b94,
	 0x2b66,  0x2b37,  0x2b09,  0x2ada,  0x2aab,  0x2a7c,  0x2a4c,  0x2a1d,
	 0x29ed,  0x29be,  0x298e,  0x295e,  0x292e,  0x28fe,  0x28ce,  0x289d,
	 0x286d,  0x283c,  0x280b,  0x27da,  0x27a9,  0x2777,  0x2746,  0x2714,
	 0x26e2,  0x26b0,  0x267e,  0x264c,  0x2619,  0x25e7,  0x25b4,  0x2581,
	 0x254d,  0x251a,  0x24e6,  0x24b2,  0x247e,  0x244a,  0x2415,  0x23e1,
	 0x23ac,  0x2376,  0x2341,  0x230b,  0x22d6,  0x229f,  0x2269,  0x2232,
	 0x21fc,  0x21c4,  0x218d,  0x2155,  0x211d,  0x20e5,  0x20ad,  0x2074,
	 0x203b,  0x2001,  0x1fc7,  0x1f8d,  0x1f53,  0x1f18,  0x1edd,  0x1ea1,
	 0x1e66,  0x1e29,  0x1ded,  0x1db0,  0x1d72,  0x1d35,  0x1cf6,  0x1cb8,
	 0x1c79,  0x1c39,  0x1bf9,  0x1bb8,  0x1b77,  0x1b36,  0x1af4,  0x1ab1,
	 0x1a6e,  0x1a2a,  0x19e6,  0x19a1,  0x195c,  0x1915,  0x18ce,  0x1887,
	 0x183f,  0x17f5,  0x17ac,  0x1761,  0x1715,  0x16c9,  0x167c,  0x162e,
	 0x15df,  0x158e,  0x153d,  0x14eb,  0x1497,  0x1442,  0x13ec,  0x1395,
	 0x133c,  0x12e2,  0x1286,  0x1228,  0x11c9,  0x1167,  0x1104,  0x109e,
	 0x1036,  0x0fcc,  0x0f5f,  0x0eef,  0x0e7b,  0x0e04,  0x0d89,  0x0d0a,
	 0x0c86,  0x0bfd,  0x0b6d,  0x0ad6,  0x0a36,  0x098d,  0x08d7,  0x0811,
	 0x0736,  0x063e,  0x0519,  0x039a,  0x0000,  0x7fff,  0x0100,  0x0080,
	 0x021d,  0x00c8,  0x00ce,  0x0048,  0x0a26,  0x277a,  0x00ce,  0x6488,
	 0x14ac,  0x0001,  0x00f9,  0x00fc,  0x00ff,  0x00fc,  0x00f9,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,
	 0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff,  0xffff
};
const int16 Dsp1::kMulTable[256] = {
	 0x0000,  0x0003,  0x0006,  0x0009,  0x000c,  0x000f,  0x0012,  0x0015,
	 0x0019,  0x001c,  0x001f,  0x0022,  0x0025,  0x0028,  0x002b,  0x002f,
	 0x0032,  0x0035,  0x0038,  0x003b,  0x003e,  0x0041,  0x0045,  0x0048,
	 0x004b,  0x004e,  0x0051,  0x0054,  0x0057,  0x005b,  0x005e,  0x0061,
	 0x0064,  0x0067,  0x006a,  0x006d,  0x0071,  0x0074,  0x0077,  0x007a,
	 0x007d,  0x0080,  0x0083,  0x0087,  0x008a,  0x008d,  0x0090,  0x0093,
	 0x0096,  0x0099,  0x009d,  0x00a0,  0x00a3,  0x00a6,  0x00a9,  0x00ac,
	 0x00af,  0x00b3,  0x00b6,  0x00b9,  0x00bc,  0x00bf,  0x00c2,  0x00c5,
	 0x00c9,  0x00cc,  0x00cf,  0x00d2,  0x00d5,  0x00d8,  0x00db,  0x00df,
	 0x00e2,  0x00e5,  0x00e8,  0x00eb,  0x00ee,  0x00f1,  0x00f5,  0x00f8,
	 0x00fb,  0x00fe,  0x0101,  0x0104,  0x0107,  0x010b,  0x010e,  0x0111,
	 0x0114,  0x0117,  0x011a,  0x011d,  0x0121,  0x0124,  0x0127,  0x012a,
	 0x012d,  0x0130,  0x0133,  0x0137,  0x013a,  0x013d,  0x0140,  0x0143,
	 0x0146,  0x0149,  0x014d,  0x0150,  0x0153,  0x0156,  0x0159,  0x015c,
	 0x015f,  0x0163,  0x0166,  0x0169,  0x016c,  0x016f,  0x0172,  0x0175,
	 0x0178,  0x017c,  0x017f,  0x0182,  0x0185,  0x0188,  0x018b,  0x018e,
	 0x0192,  0x0195,  0x0198,  0x019b,  0x019e,  0x01a1,  0x01a4,  0x01a8,
	 0x01ab,  0x01ae,  0x01b1,  0x01b4,  0x01b7,  0x01ba,  0x01be,  0x01c1,
	 0x01c4,  0x01c7,  0x01ca,  0x01cd,  0x01d0,  0x01d4,  0x01d7,  0x01da,
	 0x01dd,  0x01e0,  0x01e3,  0x01e6,  0x01ea,  0x01ed,  0x01f0,  0x01f3,
	 0x01f6,  0x01f9,  0x01fc,  0x0200,  0x0203,  0x0206,  0x0209,  0x020c,
	 0x020f,  0x0212,  0x0216,  0x0219,  0x021c,  0x021f,  0x0222,  0x0225,
	 0x0228,  0x022c,  0x022f,  0x0232,  0x0235,  0x0238,  0x023b,  0x023e,
	 0x0242,  0x0245,  0x0248,  0x024b,  0x024e,  0x0251,  0x0254,  0x0258,
	 0x025b,  0x025e,  0x0261,  0x0264,  0x0267,  0x026a,  0x026e,  0x0271,
	 0x0274,  0x0277,  0x027a,  0x027d,  0x0280,  0x0284,  0x0287,  0x028a,
	 0x028d,  0x0290,  0x0293,  0x0296,  0x029a,  0x029d,  0x02a0,  0x02a3,
	 0x02a6,  0x02a9,  0x02ac,  0x02b0,  0x02b3,  0x02b6,  0x02b9,  0x02bc,
	 0x02bf,  0x02c2,  0x02c6,  0x02c9,  0x02cc,  0x02cf,  0x02d2,  0x02d5,
	 0x02d8,  0x02db,  0x02df,  0x02e2,  0x02e5,  0x02e8,  0x02eb,  0x02ee,
	 0x02f1,  0x02f5,  0x02f8,  0x02fb,  0x02fe,  0x0301,  0x0304,  0x0307,
	 0x030b,  0x030e,  0x0311,  0x0314,  0x0317,  0x031a,  0x031d,  0x0321
};
const int16 Dsp1::kSinTable[256] = {
	 0x0000,  0x0324,  0x0647,  0x096a,  0x0c8b,  0x0fab,  0x12c8,  0x15e2,
	 0x18f8,  0x1c0b,  0x1f19,  0x2223,  0x2528,  0x2826,  0x2b1f,  0x2e11,
	 0x30fb,  0x33de,  0x36ba,  0x398c,  0x3c56,  0x3f17,  0x41ce,  0x447a,
	 0x471c,  0x49b4,  0x4c3f,  0x4ebf,  0x5133,  0x539b,  0x55f5,  0x5842,
	 0x5a82,  0x5cb4,  0x5ed7,  0x60ec,  0x62f2,  0x64e8,  0x66cf,  0x68a6,
	 0x6a6d,  0x6c24,  0x6dca,  0x6f5f,  0x70e2,  0x7255,  0x73b5,  0x7504,
	 0x7641,  0x776c,  0x7884,  0x798a,  0x7a7d,  0x7b5d,  0x7c29,  0x7ce3,
	 0x7d8a,  0x7e1d,  0x7e9d,  0x7f09,  0x7f62,  0x7fa7,  0x7fd8,  0x7ff6,
	 0x7fff,  0x7ff6,  0x7fd8,  0x7fa7,  0x7f62,  0x7f09,  0x7e9d,  0x7e1d,
	 0x7d8a,  0x7ce3,  0x7c29,  0x7b5d,  0x7a7d,  0x798a,  0x7884,  0x776c,
	 0x7641,  0x7504,  0x73b5,  0x7255,  0x70e2,  0x6f5f,  0x6dca,  0x6c24,
	 0x6a6d,  0x68a6,  0x66cf,  0x64e8,  0x62f2,  0x60ec,  0x5ed7,  0x5cb4,
	 0x5a82,  0x5842,  0x55f5,  0x539b,  0x5133,  0x4ebf,  0x4c3f,  0x49b4,
	 0x471c,  0x447a,  0x41ce,  0x3f17,  0x3c56,  0x398c,  0x36ba,  0x33de,
	 0x30fb,  0x2e11,  0x2b1f,  0x2826,  0x2528,  0x2223,  0x1f19,  0x1c0b,
	 0x18f8,  0x15e2,  0x12c8,  0x0fab,  0x0c8b,  0x096a,  0x0647,  0x0324,
	-0x0000, -0x0324, -0x0647, -0x096a, -0x0c8b, -0x0fab, -0x12c8, -0x15e2,
	-0x18f8, -0x1c0b, -0x1f19, -0x2223, -0x2528, -0x2826, -0x2b1f, -0x2e11,
	-0x30fb, -0x33de, -0x36ba, -0x398c, -0x3c56, -0x3f17, -0x41ce, -0x447a,
	-0x471c, -0x49b4, -0x4c3f, -0x4ebf, -0x5133, -0x539b, -0x55f5, -0x5842,
	-0x5a82, -0x5cb4, -0x5ed7, -0x60ec, -0x62f2, -0x64e8, -0x66cf, -0x68a6,
	-0x6a6d, -0x6c24, -0x6dca, -0x6f5f, -0x70e2, -0x7255, -0x73b5, -0x7504,
	-0x7641, -0x776c, -0x7884, -0x798a, -0x7a7d, -0x7b5d, -0x7c29, -0x7ce3,
	-0x7d8a, -0x7e1d, -0x7e9d, -0x7f09, -0x7f62, -0x7fa7, -0x7fd8, -0x7ff6,
	-0x7fff, -0x7ff6, -0x7fd8, -0x7fa7, -0x7f62, -0x7f09, -0x7e9d, -0x7e1d,
	-0x7d8a, -0x7ce3, -0x7c29, -0x7b5d, -0x7a7d, -0x798a, -0x7884, -0x776c,
	-0x7641, -0x7504, -0x73b5, -0x7255, -0x70e2, -0x6f5f, -0x6dca, -0x6c24,
	-0x6a6d, -0x68a6, -0x66cf, -0x64e8, -0x62f2, -0x60ec, -0x5ed7, -0x5cb4,
	-0x5a82, -0x5842, -0x55f5, -0x539b, -0x5133, -0x4ebf, -0x4c3f, -0x49b4,
	-0x471c, -0x447a, -0x41ce, -0x3f17, -0x3c56, -0x398c, -0x36ba, -0x33de,
	-0x30fb, -0x2e11, -0x2b1f, -0x2826, -0x2528, -0x2223, -0x1f19, -0x1c0b,
	-0x18f8, -0x15e2, -0x12c8, -0x0fab, -0x0c8b, -0x096a, -0x0647, -0x0324
};
void Dsp1::op00 (void)
{
	op00Result_ = op00Multiplicand_ * op00Multiplier_ >> 15;


}

void Dsp1::op20 (void)
{
	op20Result_ = op20Multiplicand_ * op20Multiplier_ >> 15;
	op20Result_++;


}

void Dsp1::dspInverse (int16 Coefficient, int16 Exponent, int16 *iCoefficient, int16 *iExponent)
{
	// Step One: Division by Zero
	if (Coefficient == 0x0000)
	{
		*iCoefficient = 0x7fff;
		*iExponent    = 0x002f;
	}
	else
	{
		int16	Sign = 1;

		// Step Two: Remove Sign
		if (Coefficient < 0)
		{
			if (Coefficient < -32767)
				Coefficient = -32767;
			Coefficient = -Coefficient;
			Sign = -1;
		}

		// Step Three: Normalize
#ifdef __GNUC__
		const int shift = __builtin_clz(Coefficient) - (8 * sizeof(int) - 15);
		Coefficient <<= shift;
		Exponent -= shift;
#else
		while (Coefficient < 0x4000)
		{
			Coefficient <<= 1;
			Exponent--;
		}
#endif

		// Step Four: Special Case
		if (Coefficient == 0x4000)
		{
			if (Sign == 1)
				*iCoefficient =  0x7fff;
			else
			{
				*iCoefficient = -0x4000;
				Exponent--;
			}
		}
		else
		{
			// Step Five: Initial Guess
			int16	i = kRom[((Coefficient - 0x4000) >> 7) + 0x0065];

			// Step Six: Iterate "estimated" Newton's Method
			i = (i + (-i * (Coefficient * i >> 15) >> 15)) << 1;
			i = (i + (-i * (Coefficient * i >> 15) >> 15)) << 1;

			*iCoefficient = i * Sign;
		}

		*iExponent = 1 - Exponent;
	}
}

void Dsp1::op10 (void)
{
	dspInverse(op10Coeff_, op10Exp_, &op10CoeffR_, &op10ExpR_);


}

int16 Dsp1::dspSin (int16 Angle)
{
	int32	S;

	if (Angle < 0)
	{
		if (Angle == -32768)
			return (0);

		return (-dspSin(-Angle));
	}

	S = kSinTable[Angle >> 8] + (kMulTable[Angle & 0xff] * kSinTable[0x40 + (Angle >> 8)] >> 15);
	if (S > 32767)
		S = 32767;

	return ((int16) S);
}

int16 Dsp1::dspCos (int16 Angle)
{
	int32	S;

	if (Angle < 0)
	{
		if (Angle == -32768)
			return (-32768);

		Angle = -Angle;
	}

	S = kSinTable[0x40 + (Angle >> 8)] - (kMulTable[Angle & 0xff] * kSinTable[Angle >> 8] >> 15);
	if (S < -32768)
		S = -32767;

	return ((int16) S);
}

void Dsp1::dspNormalize (int16 m, int16 *Coefficient, int16 *Exponent)
{
	int16	e = 0;

#ifdef __GNUC__
	int16	n = m < 0 ? ~m : m;

	if (n == 0)
		e = 15;
	else
		e = __builtin_clz(n) - (8 * sizeof(int) - 15);
#else
	int16	i = 0x4000;

	if (m < 0)
	{
		while ((m & i) && i)
		{
			i >>= 1;
			e++;
		}
	}
	else
	{
		while (!(m & i) && i)
		{
			i >>= 1;
			e++;
		}
	}
#endif

	if (e > 0)
		*Coefficient = m * kRom[0x21 + e] << 1;
	else
		*Coefficient = m;

	*Exponent -= e;
}

void Dsp1::dspNormalizeDouble (int32 Product, int16 *Coefficient, int16 *Exponent)
{
	int16	n = Product & 0x7fff;
	int16	m = Product >> 15;
	int16	e = 0;

#ifdef __GNUC__
	int16	t = m < 0 ? ~m : m;

	if (t == 0)
		e = 15;
	else
		e = __builtin_clz(t) - (8 * sizeof(int) - 15);
#else
	int16	i = 0x4000;

	if (m < 0)
	{
		while ((m & i) && i)
		{
			i >>= 1;
			e++;
		}
	}
	else
	{
		while (!(m & i) && i)
		{
			i >>= 1;
			e++;
		}
	}
#endif

	if (e > 0)
	{
		*Coefficient = m * kRom[0x0021 + e] << 1;

		if (e < 15)
			*Coefficient += n * kRom[0x0040 - e] >> 15;
		else
		{
#ifdef __GNUC__
			t = m < 0 ? ~(n | 0x8000) : n;

			if (t == 0)
				e += 15;
			else
				e += __builtin_clz(t) - (8 * sizeof(int) - 15);
#else
			i = 0x4000;

			if (m < 0)
			{
				while ((n & i) && i)
				{
					i >>= 1;
					e++;
				}
			}
			else
			{
				while (!(n & i) && i)
				{
					i >>= 1;
					e++;
				}
			}
#endif

			if (e > 15)
				*Coefficient = n * kRom[0x0012 + e] << 1;
			else
				*Coefficient += n;
		}
	}
	else
		*Coefficient = m;

	*Exponent = e;
}

int16 Dsp1::dspTruncate (int16 C, int16 E)
{
	if (E > 0)
	{
		if (C > 0)
			return (32767);
		else
		if (C < 0)
			return (-32767);
	}
	else
	{
		if (E < 0)
			return (C * kRom[0x0031 + E] >> 15);
	}

	return (C);
}

void Dsp1::op04 (void)
{
	op04Sin_ = dspSin(op04Angle_) * op04Radius_ >> 15;
	op04Cos_ = dspCos(op04Angle_) * op04Radius_ >> 15;
}

void Dsp1::op0C (void)
{
	op0CX2_ = (op0CY1_ * dspSin(op0CA_) >> 15) + (op0CX1_ * dspCos(op0CA_) >> 15);
	op0CY2_ = (op0CY1_ * dspCos(op0CA_) >> 15) - (op0CX1_ * dspSin(op0CA_) >> 15);
}

void Dsp1::dspParameter (int16 Fx, int16 Fy, int16 Fz, int16 Lfe, int16 Les, int16 Aas, int16 Azs, int16 *Vof, int16 *Vva, int16 *Cx, int16 *Cy)
{
	static const int16	MaxAZS_Exp[16] =
	{
		0x38b4, 0x38b7, 0x38ba, 0x38be, 0x38c0, 0x38c4, 0x38c7, 0x38ca,
		0x38ce,	0x38d0, 0x38d4, 0x38d7, 0x38da, 0x38dd, 0x38e0, 0x38e4
	};

	int16	CSec, C, E, MaxAZS, Aux;
	int16	LfeNx, LfeNy, LfeNz;
	int16	LesNx, LesNy, LesNz;
	int16	CentreZ;

	// Copy Zenith angle for clipping
	int16	AZS = Azs;

	// Store Sine and Cosine of Azimuth and Zenith angle
	sinAas_ = dspSin(Aas);
	cosAas_ = dspCos(Aas);
	sinAzs_ = dspSin(Azs);
	cosAzs_ = dspCos(Azs);

	nx_ = sinAzs_ * -sinAas_ >> 15;
	ny_ = sinAzs_ *  cosAas_ >> 15;
	nz_ = cosAzs_ *  0x7fff >> 15;

	LfeNx = Lfe * nx_ >> 15;
	LfeNy = Lfe * ny_ >> 15;
	LfeNz = Lfe * nz_ >> 15;

	// Center of Projection
	centreX_ = Fx + LfeNx;
	centreY_ = Fy + LfeNy;
	CentreZ = Fz + LfeNz;

	LesNx = Les * nx_ >> 15;
	LesNy = Les * ny_ >> 15;
	LesNz = Les * nz_ >> 15;

	gx_ = centreX_ - LesNx;
	gy_ = centreY_ - LesNy;
	gz_ = CentreZ - LesNz;

	eLes_ = 0;
	dspNormalize(Les, &cLes_, &eLes_);
	gLes_ = Les;

	E = 0;
	dspNormalize(CentreZ, &C, &E);

	vPlaneC_ = C;
	vPlaneE_ = E;

	// Determine clip boundary and clip Zenith angle if necessary
	MaxAZS = MaxAZS_Exp[-E];

	if (AZS < 0)
	{
		MaxAZS = -MaxAZS;
		if (AZS < MaxAZS + 1)
			AZS = MaxAZS + 1;
	}
	else
	{
		if (AZS > MaxAZS)
			AZS = MaxAZS;
	}

	// Store Sine and Cosine of clipped Zenith angle
	sinAZS_ = dspSin(AZS);
	cosAZS_ = dspCos(AZS);

	dspInverse(cosAZS_, 0, &secAzsC1_, &secAzsE1_);
	dspNormalize(C * secAzsC1_ >> 15, &C, &E);
	E += secAzsE1_;

	C = dspTruncate(C, E) * sinAZS_ >> 15;

	centreX_ += C * sinAas_ >> 15;
	centreY_ -= C * cosAas_ >> 15;

	*Cx = centreX_;
	*Cy = centreY_;

	// Raster number of imaginary center and horizontal line
	*Vof = 0;

	if ((Azs != AZS) || (Azs == MaxAZS))
	{
		if (Azs == -32768)
			Azs = -32767;

		C = Azs - MaxAZS;
		if (C >= 0)
			C--;
		Aux = ~(C << 2);

		C = Aux * kRom[0x0328] >> 15;
		C = (C * Aux >> 15) + kRom[0x0327];
		*Vof -= (C * Aux >> 15) * Les >> 15;

		C = Aux * Aux >> 15;
		Aux = (C * kRom[0x0324] >> 15) + kRom[0x0325];
		cosAZS_ += (C * Aux >> 15) * cosAZS_ >> 15;
	}

	vOffset_ = Les * cosAZS_ >> 15;

	dspInverse(sinAZS_, 0, &CSec, &E);
	dspNormalize(vOffset_, &C, &E);
	dspNormalize(C * CSec >> 15, &C, &E);

	if (C == -32768)
	{
		C >>= 1;
		E++;
	}

	*Vva = dspTruncate(-C, E);

	// Store Secant of clipped Zenith angle
	dspInverse(cosAZS_, 0, &secAzsC2_, &secAzsE2_);
}

void Dsp1::dspRaster (int16 Vs, int16 *An, int16 *Bn, int16 *Cn, int16 *Dn)
{
	int16	C, E, C1, E1;

	dspInverse((Vs * sinAzs_ >> 15) + vOffset_, 7, &C, &E);
	E += vPlaneE_;

	C1 = C * vPlaneC_ >> 15;
	E1 = E + secAzsE2_;

	dspNormalize(C1, &C, &E);

	C = dspTruncate(C, E);

	*An = C *  cosAas_ >> 15;
	*Cn = C *  sinAas_ >> 15;

	dspNormalize(C1 * secAzsC2_ >> 15, &C, &E1);

	C = dspTruncate(C, E1);

	*Bn = C * -sinAas_ >> 15;
	*Dn = C *  cosAas_ >> 15;
}

void Dsp1::op02 (void)
{
	dspParameter(op02FX_, op02FY_, op02FZ_, op02LFE_, op02LES_, op02AAS_, op02AZS_, &op02VOF_, &op02VVA_, &op02CX_, &op02CY_);
}

void Dsp1::op0A (void)
{
	dspRaster(op0AVS_, &op0AA_, &op0AB_, &op0AC_, &op0AD_);
	op0AVS_++;
}

int16 Dsp1::dspShiftR (int16 C, int16 E)
{
	return (C * kRom[0x0031 + E] >> 15);
}

void Dsp1::dspProject (int16 X, int16 Y, int16 Z, int16 *H, int16 *V, int16 *M)
{
	int32	aux, aux4;
	int16	E, E2, E3, E4, E5, refE, E6, E7;
	int16	C2, C4, C6, C8, C9, C10, C11, C12, C16, C17, C18, C19, C20, C21, C22, C23, C24, C25, C26;
	int16	Px, Py, Pz;

	E4 = E3 = E2 = E = E5 = 0;

	dspNormalizeDouble((int32) X - gx_, &Px, &E4);
	dspNormalizeDouble((int32) Y - gy_, &Py, &E );
	dspNormalizeDouble((int32) Z - gz_, &Pz, &E3);
	Px >>= 1; // to avoid overflows when calculating the scalar products
	E4--;
	Py >>= 1;
	E--;
	Pz >>= 1;
	E3--;

	refE = (E < E3) ? E : E3;
	refE = (refE < E4) ? refE : E4;

	Px = dspShiftR(Px, E4 - refE); // normalize them to the same exponent
	Py = dspShiftR(Py, E  - refE);
	Pz = dspShiftR(Pz, E3 - refE);

	C11 = -(Px * nx_ >> 15);
	C8  = -(Py * ny_ >> 15);
	C9  = -(Pz * nz_ >> 15);
	C12 = C11 + C8 + C9; // this cannot overflow!

	aux4 = C12; // de-normalization with 32-bits arithmetic
	refE = 16 - refE; // refE can be up to 3
	if (refE >= 0)
		aux4 <<=  (refE);
	else
		aux4 >>= -(refE);
	if (aux4 == -1)
		aux4 = 0; // why?
	aux4 >>= 1;

	aux = ((uint16) gLes_) + aux4; // Les - the scalar product of P with the normal vector of the screen
	dspNormalizeDouble(aux, &C10, &E2);
	E2 = 15 - E2;

	dspInverse(C10, 0, &C4, &E4);
	C2 = C4 * cLes_ >> 15; // scale factor

	// H
	E7 = 0;
	C16 = Px * ( cosAas_ *  0x7fff >> 15) >> 15;
	C20 = Py * ( sinAas_ *  0x7fff >> 15) >> 15;
	C17 = C16 + C20; // scalar product of P with the normalized horizontal vector of the screen...

	C18 = C17 * C2 >> 15; // ... multiplied by the scale factor
	dspNormalize(C18, &C19, &E7);
	*H = dspTruncate(C19, eLes_ - E2 + refE + E7);

	// V
	E6 = 0;
	C21 = Px * ( cosAzs_ * -sinAas_ >> 15) >> 15;
	C22 = Py * ( cosAzs_ *  cosAas_ >> 15) >> 15;
	C23 = Pz * (-sinAzs_ *  0x7fff >> 15) >> 15;
	C24 = C21 + C22 + C23; // scalar product of P with the normalized vertical vector of the screen...

	C26 = C24 * C2 >> 15; // ... multiplied by the scale factor
	dspNormalize(C26, &C25, &E6);
	*V = dspTruncate(C25, eLes_ - E2 + refE + E6);

	// M
	dspNormalize(C2, &C6, &E4);
	*M = dspTruncate(C6, E4 + eLes_ - E2 - 7); // M is the scale factor divided by 2^7
}

void Dsp1::op06 (void)
{
	dspProject(op06X_, op06Y_, op06Z_, &op06H_, &op06V_, &op06M_);
}

void Dsp1::op01 (void)
{
	int16	SinAz = dspSin(op01Zr_);
	int16	CosAz = dspCos(op01Zr_);
	int16	SinAy = dspSin(op01Yr_);
	int16	CosAy = dspCos(op01Yr_);
	int16	SinAx = dspSin(op01Xr_);
	int16	CosAx = dspCos(op01Xr_);

	op01m_ >>= 1;

	matrixA_[0][0] =   (op01m_ * CosAz >> 15) * CosAy >> 15;
	matrixA_[0][1] = -((op01m_ * SinAz >> 15) * CosAy >> 15);
	matrixA_[0][2] =    op01m_ * SinAy >> 15;

	matrixA_[1][0] =  ((op01m_ * SinAz >> 15) * CosAx >> 15) + (((op01m_ * CosAz >> 15) * SinAx >> 15) * SinAy >> 15);
	matrixA_[1][1] =  ((op01m_ * CosAz >> 15) * CosAx >> 15) - (((op01m_ * SinAz >> 15) * SinAx >> 15) * SinAy >> 15);
	matrixA_[1][2] = -((op01m_ * SinAx >> 15) * CosAy >> 15);

	matrixA_[2][0] =  ((op01m_ * SinAz >> 15) * SinAx >> 15) - (((op01m_ * CosAz >> 15) * CosAx >> 15) * SinAy >> 15);
	matrixA_[2][1] =  ((op01m_ * CosAz >> 15) * SinAx >> 15) + (((op01m_ * SinAz >> 15) * CosAx >> 15) * SinAy >> 15);
	matrixA_[2][2] =   (op01m_ * CosAx >> 15) * CosAy >> 15;
}

void Dsp1::op11 (void)
{
	int16	SinAz = dspSin(op11Zr_);
	int16	CosAz = dspCos(op11Zr_);
	int16	SinAy = dspSin(op11Yr_);
	int16	CosAy = dspCos(op11Yr_);
	int16	SinAx = dspSin(op11Xr_);
	int16	CosAx = dspCos(op11Xr_);

	op11m_ >>= 1;

	matrixB_[0][0] =   (op11m_ * CosAz >> 15) * CosAy >> 15;
	matrixB_[0][1] = -((op11m_ * SinAz >> 15) * CosAy >> 15);
	matrixB_[0][2] =    op11m_ * SinAy >> 15;

	matrixB_[1][0] =  ((op11m_ * SinAz >> 15) * CosAx >> 15) + (((op11m_ * CosAz >> 15) * SinAx >> 15) * SinAy >> 15);
	matrixB_[1][1] =  ((op11m_ * CosAz >> 15) * CosAx >> 15) - (((op11m_ * SinAz >> 15) * SinAx >> 15) * SinAy >> 15);
	matrixB_[1][2] = -((op11m_ * SinAx >> 15) * CosAy >> 15);

	matrixB_[2][0] =  ((op11m_ * SinAz >> 15) * SinAx >> 15) - (((op11m_ * CosAz >> 15) * CosAx >> 15) * SinAy >> 15);
	matrixB_[2][1] =  ((op11m_ * CosAz >> 15) * SinAx >> 15) + (((op11m_ * SinAz >> 15) * CosAx >> 15) * SinAy >> 15);
	matrixB_[2][2] =   (op11m_ * CosAx >> 15) * CosAy >> 15;
}

void Dsp1::op21 (void)
{
	int16	SinAz = dspSin(op21Zr_);
	int16	CosAz = dspCos(op21Zr_);
	int16	SinAy = dspSin(op21Yr_);
	int16	CosAy = dspCos(op21Yr_);
	int16	SinAx = dspSin(op21Xr_);
	int16	CosAx = dspCos(op21Xr_);

	op21m_ >>= 1;

	matrixC_[0][0] =   (op21m_ * CosAz >> 15) * CosAy >> 15;
	matrixC_[0][1] = -((op21m_ * SinAz >> 15) * CosAy >> 15);
	matrixC_[0][2] =    op21m_ * SinAy >> 15;

	matrixC_[1][0] =  ((op21m_ * SinAz >> 15) * CosAx >> 15) + (((op21m_ * CosAz >> 15) * SinAx >> 15) * SinAy >> 15);
	matrixC_[1][1] =  ((op21m_ * CosAz >> 15) * CosAx >> 15) - (((op21m_ * SinAz >> 15) * SinAx >> 15) * SinAy >> 15);
	matrixC_[1][2] = -((op21m_ * SinAx >> 15) * CosAy >> 15);

	matrixC_[2][0] =  ((op21m_ * SinAz >> 15) * SinAx >> 15) - (((op21m_ * CosAz >> 15) * CosAx >> 15) * SinAy >> 15);
	matrixC_[2][1] =  ((op21m_ * CosAz >> 15) * SinAx >> 15) + (((op21m_ * SinAz >> 15) * CosAx >> 15) * SinAy >> 15);
	matrixC_[2][2] =   (op21m_ * CosAx >> 15) * CosAy >> 15;
}

void Dsp1::op0D (void)
{
	op0DF_ = (op0DX_ * matrixA_[0][0] >> 15) + (op0DY_ * matrixA_[0][1] >> 15) + (op0DZ_ * matrixA_[0][2] >> 15);
	op0DL_ = (op0DX_ * matrixA_[1][0] >> 15) + (op0DY_ * matrixA_[1][1] >> 15) + (op0DZ_ * matrixA_[1][2] >> 15);
	op0DU_ = (op0DX_ * matrixA_[2][0] >> 15) + (op0DY_ * matrixA_[2][1] >> 15) + (op0DZ_ * matrixA_[2][2] >> 15);


}

void Dsp1::op1D (void)
{
	op1DF_ = (op1DX_ * matrixB_[0][0] >> 15) + (op1DY_ * matrixB_[0][1] >> 15) + (op1DZ_ * matrixB_[0][2] >> 15);
	op1DL_ = (op1DX_ * matrixB_[1][0] >> 15) + (op1DY_ * matrixB_[1][1] >> 15) + (op1DZ_ * matrixB_[1][2] >> 15);
	op1DU_ = (op1DX_ * matrixB_[2][0] >> 15) + (op1DY_ * matrixB_[2][1] >> 15) + (op1DZ_ * matrixB_[2][2] >> 15);


}

void Dsp1::op2D (void)
{
	op2DF_ = (op2DX_ * matrixC_[0][0] >> 15) + (op2DY_ * matrixC_[0][1] >> 15) + (op2DZ_ * matrixC_[0][2] >> 15);
	op2DL_ = (op2DX_ * matrixC_[1][0] >> 15) + (op2DY_ * matrixC_[1][1] >> 15) + (op2DZ_ * matrixC_[1][2] >> 15);
	op2DU_ = (op2DX_ * matrixC_[2][0] >> 15) + (op2DY_ * matrixC_[2][1] >> 15) + (op2DZ_ * matrixC_[2][2] >> 15);


}

void Dsp1::op03 (void)
{
	op03X_ = (op03F_ * matrixA_[0][0] >> 15) + (op03L_ * matrixA_[1][0] >> 15) + (op03U_ * matrixA_[2][0] >> 15);
	op03Y_ = (op03F_ * matrixA_[0][1] >> 15) + (op03L_ * matrixA_[1][1] >> 15) + (op03U_ * matrixA_[2][1] >> 15);
	op03Z_ = (op03F_ * matrixA_[0][2] >> 15) + (op03L_ * matrixA_[1][2] >> 15) + (op03U_ * matrixA_[2][2] >> 15);


}

void Dsp1::op13 (void)
{
	op13X_ = (op13F_ * matrixB_[0][0] >> 15) + (op13L_ * matrixB_[1][0] >> 15) + (op13U_ * matrixB_[2][0] >> 15);
	op13Y_ = (op13F_ * matrixB_[0][1] >> 15) + (op13L_ * matrixB_[1][1] >> 15) + (op13U_ * matrixB_[2][1] >> 15);
	op13Z_ = (op13F_ * matrixB_[0][2] >> 15) + (op13L_ * matrixB_[1][2] >> 15) + (op13U_ * matrixB_[2][2] >> 15);


}

void Dsp1::op23 (void)
{
	op23X_ = (op23F_ * matrixC_[0][0] >> 15) + (op23L_ * matrixC_[1][0] >> 15) + (op23U_ * matrixC_[2][0] >> 15);
	op23Y_ = (op23F_ * matrixC_[0][1] >> 15) + (op23L_ * matrixC_[1][1] >> 15) + (op23U_ * matrixC_[2][1] >> 15);
	op23Z_ = (op23F_ * matrixC_[0][2] >> 15) + (op23L_ * matrixC_[1][2] >> 15) + (op23U_ * matrixC_[2][2] >> 15);


}

void Dsp1::op14 (void)
{
	int16	CSec, ESec, CTan, CSin, C, E;

	dspInverse(dspCos(op14Xr_), 0, &CSec, &ESec);

	// Rotation Around Z
	dspNormalizeDouble(op14U_ * dspCos(op14Yr_) - op14F_ * dspSin(op14Yr_), &C, &E);

	E = ESec - E;

	dspNormalize(C * CSec >> 15, &C, &E);

	op14Zrr_ = op14Zr_ + dspTruncate(C, E);

	// Rotation Around X
	op14Xrr_ = op14Xr_ + (op14U_ * dspSin(op14Yr_) >> 15) + (op14F_ * dspCos(op14Yr_) >> 15);

	// Rotation Around Y
	dspNormalizeDouble(op14U_ * dspCos(op14Yr_) + op14F_ * dspSin(op14Yr_), &C, &E);

	E = ESec - E;

	dspNormalize(dspSin(op14Xr_), &CSin, &E);

	CTan = CSec * CSin >> 15;

	dspNormalize(-(C * CTan >> 15), &C, &E);

	op14Yrr_ = op14Yr_ + dspTruncate(C, E) + op14L_;
}

void Dsp1::dspTarget (int16 H, int16 V, int16 *X, int16 *Y)
{
	int16	C, E, C1, E1;

	dspInverse((V * sinAzs_ >> 15) + vOffset_, 8, &C, &E);
	E += vPlaneE_;

	C1 = C * vPlaneC_ >> 15;
	E1 = E + secAzsE1_;

	H <<= 8;

	dspNormalize(C1, &C, &E);

	C = dspTruncate(C, E) * H >> 15;

	*X = centreX_ + (C * cosAas_ >> 15);
	*Y = centreY_ - (C * sinAas_ >> 15);

	V <<= 8;

	dspNormalize(C1 * secAzsC1_ >> 15, &C, &E1);

	C = dspTruncate(C, E1) * V >> 15;

	*X += C * -sinAas_ >> 15;
	*Y += C *  cosAas_ >> 15;
}

void Dsp1::op0E (void)
{
	dspTarget(op0EH_, op0EV_, &op0EX_, &op0EY_);
}

void Dsp1::op0B (void)
{
	op0BS_ = (op0BX_ * matrixA_[0][0] + op0BY_ * matrixA_[0][1] + op0BZ_ * matrixA_[0][2]) >> 15;


}

void Dsp1::op1B (void)
{
	op1BS_ = (op1BX_ * matrixB_[0][0] + op1BY_ * matrixB_[0][1] + op1BZ_ * matrixB_[0][2]) >> 15;


}

void Dsp1::op2B (void)
{
	op2BS_ = (op2BX_ * matrixC_[0][0] + op2BY_ * matrixC_[0][1] + op2BZ_ * matrixC_[0][2]) >> 15;


}

void Dsp1::op08 (void)
{
	int32	op08Size = (op08X_ * op08X_ + op08Y_ * op08Y_ + op08Z_ * op08Z_) << 1;
	op08Ll_ =  op08Size        & 0xffff;
	op08Lh_ = (op08Size >> 16) & 0xffff;


}

void Dsp1::op18 (void)
{
	op18D_ = (op18X_ * op18X_ + op18Y_ * op18Y_ + op18Z_ * op18Z_ - op18R_ * op18R_) >> 15;


}

void Dsp1::op38 (void)
{
	op38D_ = (op38X_ * op38X_ + op38Y_ * op38Y_ + op38Z_ * op38Z_ - op38R_ * op38R_) >> 15;
	op38D_++;


}

void Dsp1::op28 (void)
{
	int32	Radius = op28X_ * op28X_ + op28Y_ * op28Y_ + op28Z_ * op28Z_;

	if (Radius == 0)
		op28R_ = 0;
	else
	{
		int16	C, E, Pos, Node1, Node2;

		dspNormalizeDouble(Radius, &C, &E);
		if (E & 1)
			C = C * 0x4000 >> 15;

		Pos = C * 0x0040 >> 15;

		Node1 = kRom[0x00d5 + Pos];
		Node2 = kRom[0x00d6 + Pos];

		op28R_ = ((Node2 - Node1) * (C & 0x1ff) >> 9) + Node1;
		op28R_ >>= (E >> 1);
	}


}

void Dsp1::op1C (void)
{
	// Rotate Around Op1CZ1
	op1CX1_ = (op1CYBR_ * dspSin(op1CZ_) >> 15) + (op1CXBR_ * dspCos(op1CZ_) >> 15);
	op1CY1_ = (op1CYBR_ * dspCos(op1CZ_) >> 15) - (op1CXBR_ * dspSin(op1CZ_) >> 15);
	op1CXBR_ = op1CX1_;
	op1CYBR_ = op1CY1_;

	// Rotate Around Op1CY1
	op1CZ1_ = (op1CXBR_ * dspSin(op1CY_) >> 15) + (op1CZBR_ * dspCos(op1CY_) >> 15);
	op1CX1_ = (op1CXBR_ * dspCos(op1CY_) >> 15) - (op1CZBR_ * dspSin(op1CY_) >> 15);
	op1CXAR_ = op1CX1_;
	op1CZBR_ = op1CZ1_;

	// Rotate Around Op1CX1
	op1CY1_ = (op1CZBR_ * dspSin(op1CX_) >> 15) + (op1CYBR_ * dspCos(op1CX_) >> 15);
	op1CZ1_ = (op1CZBR_ * dspCos(op1CX_) >> 15) - (op1CYBR_ * dspSin(op1CX_) >> 15);
	op1CYAR_ = op1CY1_;
	op1CZAR_ = op1CZ1_;


}

void Dsp1::op0F (void)
{
	op0FPass_ = 0x0000;


}

void Dsp1::op2F (void)
{
	op2FSize_ = 0x100;
}


Dsp1::Dsp1(bool hirom): hirom_(hirom), boundary_(hirom?0x7000:0xC000) {}
auto Dsp1::handles(uint24 address) const -> bool { uint32 bank=address>>16; uint32 offs=address&0xFFFF; if(hirom_){ bool b=(bank<=0x1F)||(bank>=0x80&&bank<=0x9F); return b && offs>=0x6000 && offs<0x8000; } else { bool b=(bank>=0x30&&bank<=0x3F)||(bank>=0xB0&&bank<=0xBF); return b && offs>=0x8000; } }
void Dsp1::power(){ waiting4command_=true; firstParam_=false; command_=0; inCount_=inIndex_=outCount_=outIndex_=0; std::memset(params_,0,sizeof(params_)); std::memset(output_,0,sizeof(output_)); centreX_=centreY_=vOffset_=vPlaneC_=vPlaneE_=0; sinAas_=cosAas_=sinAzs_=cosAzs_=sinAZS_=cosAZS_=secAzsC1_=secAzsE1_=secAzsC2_=secAzsE2_=0; nx_=ny_=nz_=gx_=gy_=gz_=cLes_=eLes_=gLes_=0; std::memset(matrixA_,0,sizeof(matrixA_)); std::memset(matrixB_,0,sizeof(matrixB_)); std::memset(matrixC_,0,sizeof(matrixC_)); op00Multiplicand_=op00Multiplier_=op00Result_=0; op20Multiplicand_=op20Multiplier_=op20Result_=0; op10Coeff_=op10Exp_=op10CoeffR_=op10ExpR_=0; op04Angle_=0; op04Radius_=0; op04Sin_=op04Cos_=0; op0CA_=op0CX1_=op0CY1_=op0CX2_=op0CY2_=0; op02FX_=op02FY_=op02FZ_=op02LFE_=op02LES_=0; op02AAS_=op02AZS_=0; op02VOF_=op02VVA_=op02CX_=op02CY_=0; op0AVS_=op0AA_=op0AB_=op0AC_=op0AD_=0; op06X_=op06Y_=op06Z_=op06H_=op06V_=op06M_=0; op01m_=op01Zr_=op01Yr_=op01Xr_=op11m_=op11Zr_=op11Yr_=op11Xr_=op21m_=op21Zr_=op21Yr_=op21Xr_=0; op0DX_=op0DY_=op0DZ_=op0DF_=op0DL_=op0DU_=op1DX_=op1DY_=op1DZ_=op1DF_=op1DL_=op1DU_=op2DX_=op2DY_=op2DZ_=op2DF_=op2DL_=op2DU_=0; op03F_=op03L_=op03U_=op03X_=op03Y_=op03Z_=op13F_=op13L_=op13U_=op13X_=op13Y_=op13Z_=op23F_=op23L_=op23U_=op23X_=op23Y_=op23Z_=0; op0BX_=op0BY_=op0BZ_=op0BS_=op1BX_=op1BY_=op1BZ_=op1BS_=op2BX_=op2BY_=op2BZ_=op2BS_=0; op14Zr_=op14Xr_=op14Yr_=op14U_=op14F_=op14L_=op14Zrr_=op14Xrr_=op14Yrr_=0; op0EH_=op0EV_=op0EX_=op0EY_=0; op08X_=op08Y_=op08Z_=0; op08Ll_=op08Lh_=0; op18X_=op18Y_=op18Z_=op18R_=op18D_=op38X_=op38Y_=op38Z_=op38R_=op38D_=op28X_=op28Y_=op28Z_=op28R_=0; op1CZ_=op1CY_=op1CX_=op1CXBR_=op1CYBR_=op1CZBR_=op1CX1_=op1CY1_=op1CZ1_=op1CXAR_=op1CYAR_=op1CZAR_=0; op0FPass_=op0FRamsize_=op2FSize_=op2FUnknown_=0; }
void Dsp1::execCommand(){ outIndex_=0; switch(command_){ case 0x1f: outCount_=2048; break; case 0x00: op00Multiplicand_=rd16s(params_+0); op00Multiplier_=rd16s(params_+2); op00(); outCount_=2; wr16(output_+0, (uint16)op00Result_); break; case 0x20: op20Multiplicand_=rd16s(params_+0); op20Multiplier_=rd16s(params_+2); op20(); outCount_=2; wr16(output_+0, (uint16)op20Result_); break; case 0x30: case 0x10: op10Coeff_=rd16s(params_+0); op10Exp_=rd16s(params_+2); op10(); outCount_=4; wr16(output_+0,(uint16)op10CoeffR_); wr16(output_+2,(uint16)op10ExpR_); break; case 0x24: case 0x04: op04Angle_=rd16s(params_+0); op04Radius_=rd16(params_+2); op04(); outCount_=4; wr16(output_+0,(uint16)op04Sin_); wr16(output_+2,(uint16)op04Cos_); break; case 0x08: op08X_=rd16s(params_+0); op08Y_=rd16s(params_+2); op08Z_=rd16s(params_+4); op08(); outCount_=4; wr16(output_+0,op08Ll_); wr16(output_+2,op08Lh_); break; case 0x18: op18X_=rd16s(params_+0); op18Y_=rd16s(params_+2); op18Z_=rd16s(params_+4); op18R_=rd16s(params_+6); op18(); outCount_=2; wr16(output_+0,(uint16)op18D_); break; case 0x38: op38X_=rd16s(params_+0); op38Y_=rd16s(params_+2); op38Z_=rd16s(params_+4); op38R_=rd16s(params_+6); op38(); outCount_=2; wr16(output_+0,(uint16)op38D_); break; case 0x28: op28X_=rd16s(params_+0); op28Y_=rd16s(params_+2); op28Z_=rd16s(params_+4); op28(); outCount_=2; wr16(output_+0,(uint16)op28R_); break; case 0x2c: case 0x0c: op0CA_=rd16s(params_+0); op0CX1_=rd16s(params_+2); op0CY1_=rd16s(params_+4); op0C(); outCount_=4; wr16(output_+0,(uint16)op0CX2_); wr16(output_+2,(uint16)op0CY2_); break; case 0x3c: case 0x1c: op1CZ_=rd16(params_+0); op1CY_=rd16(params_+2); op1CX_=rd16(params_+4); op1CXBR_=rd16(params_+6); op1CYBR_=rd16(params_+8); op1CZBR_=rd16(params_+10); op1C(); outCount_=6; wr16(output_+0,(uint16)op1CXAR_); wr16(output_+2,(uint16)op1CYAR_); wr16(output_+4,(uint16)op1CZAR_); break; case 0x32: case 0x22: case 0x12: case 0x02: op02FX_=rd16s(params_+0); op02FY_=rd16s(params_+2); op02FZ_=rd16s(params_+4); op02LFE_=rd16s(params_+6); op02LES_=rd16s(params_+8); op02AAS_=rd16(params_+10); op02AZS_=rd16(params_+12); op02(); outCount_=8; wr16(output_+0,(uint16)op02VOF_); wr16(output_+2,(uint16)op02VVA_); wr16(output_+4,(uint16)op02CX_); wr16(output_+6,(uint16)op02CY_); break; case 0x3a: case 0x2a: case 0x1a: case 0x0a: op0AVS_=rd16s(params_+0); op0A(); outCount_=8; wr16(output_+0,(uint16)op0AA_); wr16(output_+2,(uint16)op0AB_); wr16(output_+4,(uint16)op0AC_); wr16(output_+6,(uint16)op0AD_); inIndex_=0; break; case 0x16: case 0x26: case 0x36: case 0x06: op06X_=rd16s(params_+0); op06Y_=rd16s(params_+2); op06Z_=rd16s(params_+4); op06(); outCount_=6; wr16(output_+0,(uint16)op06H_); wr16(output_+2,(uint16)op06V_); wr16(output_+4,(uint16)op06M_); break; case 0x1e: case 0x2e: case 0x3e: case 0x0e: op0EH_=rd16s(params_+0); op0EV_=rd16s(params_+2); op0E(); outCount_=4; wr16(output_+0,(uint16)op0EX_); wr16(output_+2,(uint16)op0EY_); break; case 0x05: case 0x35: case 0x31: case 0x01: op01m_=rd16s(params_+0); op01Zr_=rd16s(params_+2); op01Yr_=rd16s(params_+4); op01Xr_=rd16s(params_+6); op01(); break; case 0x15: case 0x11: op11m_=rd16s(params_+0); op11Zr_=rd16s(params_+2); op11Yr_=rd16s(params_+4); op11Xr_=rd16s(params_+6); op11(); break; case 0x25: case 0x21: op21m_=rd16s(params_+0); op21Zr_=rd16s(params_+2); op21Yr_=rd16s(params_+4); op21Xr_=rd16s(params_+6); op21(); break; case 0x09: case 0x39: case 0x3d: case 0x0d: op0DX_=rd16s(params_+0); op0DY_=rd16s(params_+2); op0DZ_=rd16s(params_+4); op0D(); outCount_=6; wr16(output_+0,(uint16)op0DF_); wr16(output_+2,(uint16)op0DL_); wr16(output_+4,(uint16)op0DU_); break; case 0x19: case 0x1d: op1DX_=rd16s(params_+0); op1DY_=rd16s(params_+2); op1DZ_=rd16s(params_+4); op1D(); outCount_=6; wr16(output_+0,(uint16)op1DF_); wr16(output_+2,(uint16)op1DL_); wr16(output_+4,(uint16)op1DU_); break; case 0x29: case 0x2d: op2DX_=rd16s(params_+0); op2DY_=rd16s(params_+2); op2DZ_=rd16s(params_+4); op2D(); outCount_=6; wr16(output_+0,(uint16)op2DF_); wr16(output_+2,(uint16)op2DL_); wr16(output_+4,(uint16)op2DU_); break; case 0x33: case 0x03: op03F_=rd16s(params_+0); op03L_=rd16s(params_+2); op03U_=rd16s(params_+4); op03(); outCount_=6; wr16(output_+0,(uint16)op03X_); wr16(output_+2,(uint16)op03Y_); wr16(output_+4,(uint16)op03Z_); break; case 0x13: op13F_=rd16s(params_+0); op13L_=rd16s(params_+2); op13U_=rd16s(params_+4); op13(); outCount_=6; wr16(output_+0,(uint16)op13X_); wr16(output_+2,(uint16)op13Y_); wr16(output_+4,(uint16)op13Z_); break; case 0x23: op23F_=rd16s(params_+0); op23L_=rd16s(params_+2); op23U_=rd16s(params_+4); op23(); outCount_=6; wr16(output_+0,(uint16)op23X_); wr16(output_+2,(uint16)op23Y_); wr16(output_+4,(uint16)op23Z_); break; case 0x3b: case 0x0b: op0BX_=rd16s(params_+0); op0BY_=rd16s(params_+2); op0BZ_=rd16s(params_+4); op0B(); outCount_=2; wr16(output_+0,(uint16)op0BS_); break; case 0x1b: op1BX_=rd16s(params_+0); op1BY_=rd16s(params_+2); op1BZ_=rd16s(params_+4); op1B(); outCount_=2; wr16(output_+0,(uint16)op1BS_); break; case 0x2b: op2BX_=rd16s(params_+0); op2BY_=rd16s(params_+2); op2BZ_=rd16s(params_+4); op2B(); outCount_=2; wr16(output_+0,(uint16)op2BS_); break; case 0x34: case 0x14: op14Zr_=rd16s(params_+0); op14Xr_=rd16s(params_+2); op14Yr_=rd16s(params_+4); op14U_=rd16s(params_+6); op14F_=rd16s(params_+8); op14L_=rd16s(params_+10); op14(); outCount_=6; wr16(output_+0,(uint16)op14Zrr_); wr16(output_+2,(uint16)op14Xrr_); wr16(output_+4,(uint16)op14Yrr_); break; case 0x27: case 0x2F: op2FUnknown_=rd16s(params_+0); op2F(); outCount_=2; wr16(output_+0,(uint16)op2FSize_); break; case 0x07: case 0x0F: op0FRamsize_=rd16s(params_+0); op0F(); outCount_=2; wr16(output_+0,(uint16)op0FPass_); break; default: break; } }
void Dsp1::write(uint24 address, uint8 data){ uint16 a=uint16(address & 0xFFFF); if(a>=boundary_) return; if((command_==0x0A||command_==0x1A)&&outCount_!=0){ outCount_--; outIndex_++; return; } if(waiting4command_){ command_=data; inIndex_=0; waiting4command_=false; firstParam_=true; switch(data){ case 0x00: inCount_=2; break; case 0x30: case 0x10: inCount_=2; break; case 0x20: inCount_=2; break; case 0x24: case 0x04: inCount_=2; break; case 0x08: inCount_=3; break; case 0x18: inCount_=4; break; case 0x28: inCount_=3; break; case 0x38: inCount_=4; break; case 0x2c: case 0x0c: inCount_=3; break; case 0x3c: case 0x1c: inCount_=6; break; case 0x32: case 0x22: case 0x12: case 0x02: inCount_=7; break; case 0x0a: inCount_=1; break; case 0x3a: case 0x2a: case 0x1a: command_=0x1a; inCount_=1; break; case 0x16: case 0x26: case 0x36: case 0x06: inCount_=3; break; case 0x1e: case 0x2e: case 0x3e: case 0x0e: inCount_=2; break; case 0x05: case 0x35: case 0x31: case 0x01: inCount_=4; break; case 0x15: case 0x11: inCount_=4; break; case 0x25: case 0x21: inCount_=4; break; case 0x09: case 0x39: case 0x3d: case 0x0d: inCount_=3; break; case 0x19: case 0x1d: inCount_=3; break; case 0x29: case 0x2d: inCount_=3; break; case 0x33: case 0x03: inCount_=3; break; case 0x13: inCount_=3; break; case 0x23: inCount_=3; break; case 0x3b: case 0x0b: inCount_=3; break; case 0x1b: inCount_=3; break; case 0x2b: inCount_=3; break; case 0x34: case 0x14: inCount_=6; break; case 0x07: case 0x0F: inCount_=1; break; case 0x27: case 0x2F: inCount_=1; break; case 0x17: case 0x37: case 0x3F: command_=0x1f; case 0x1f: inCount_=1; break; default: case 0x80: inCount_=0; waiting4command_=true; firstParam_=true; break; } inCount_<<=1; } else { params_[inIndex_++]=data; firstParam_=false; } if(waiting4command_ || (firstParam_ && data==0x80)){ waiting4command_=true; firstParam_=false; } else if(firstParam_ && (inCount_!=0 || (inCount_==0 && inIndex_==0))) {} else { if(inCount_) if(--inCount_==0){ waiting4command_=true; outIndex_=0; execCommand(); } } }
uint8 Dsp1::read(uint24 address){ uint16 a=uint16(address & 0xFFFF); if(a>=boundary_) return 0x80; if(outCount_){ uint8 t=output_[outIndex_++]; if(--outCount_==0){ if(command_==0x1a||command_==0x0a){ op0A(); outCount_=8; outIndex_=0; wr16(output_+0,(uint16)op0AA_); wr16(output_+2,(uint16)op0AB_); wr16(output_+4,(uint16)op0AC_); wr16(output_+6,(uint16)op0AD_); } if(command_==0x1f){ if((outIndex_&1)) t=uint8(kRom[outIndex_>>1]); else t=uint8(kRom[outIndex_>>1]>>8); } } waiting4command_=true; return t; } return 0x80; }
void Dsp1::serialize(Writer& w) const { w.b(waiting4command_); w.b(firstParam_); w.u8(command_); w.u32(inCount_); w.u32(inIndex_); w.u32(outCount_); w.u32(outIndex_); w.raw(params_,sizeof(params_)); w.raw(output_,sizeof(output_)); w.raw(matrixA_,sizeof(matrixA_)); w.raw(matrixB_,sizeof(matrixB_)); w.raw(matrixC_,sizeof(matrixC_)); auto wr=[&](int16 v){ w.u16((uint16)v); }; wr(centreX_); wr(centreY_); wr(vOffset_); wr(vPlaneC_); wr(vPlaneE_); wr(sinAas_); wr(cosAas_); wr(sinAzs_); wr(cosAzs_); wr(sinAZS_); wr(cosAZS_); wr(secAzsC1_); wr(secAzsE1_); wr(secAzsC2_); wr(secAzsE2_); wr(nx_); wr(ny_); wr(nz_); wr(gx_); wr(gy_); wr(gz_); wr(cLes_); wr(eLes_); wr(gLes_); wr(op00Multiplicand_); wr(op00Multiplier_); wr(op00Result_); wr(op20Multiplicand_); wr(op20Multiplier_); wr(op20Result_); wr(op10Coeff_); wr(op10Exp_); wr(op10CoeffR_); wr(op10ExpR_); wr(op04Angle_); w.u16(op04Radius_); wr(op04Sin_); wr(op04Cos_); wr(op0CA_); wr(op0CX1_); wr(op0CY1_); wr(op0CX2_); wr(op0CY2_); wr(op02FX_); wr(op02FY_); wr(op02FZ_); wr(op02LFE_); wr(op02LES_); w.u16(op02AAS_); w.u16(op02AZS_); wr(op02VOF_); wr(op02VVA_); wr(op02CX_); wr(op02CY_); wr(op0AVS_); wr(op0AA_); wr(op0AB_); wr(op0AC_); wr(op0AD_); wr(op06X_); wr(op06Y_); wr(op06Z_); wr(op06H_); wr(op06V_); wr(op06M_); wr(op01m_); wr(op01Zr_); wr(op01Yr_); wr(op01Xr_); wr(op11m_); wr(op11Zr_); wr(op11Yr_); wr(op11Xr_); wr(op21m_); wr(op21Zr_); wr(op21Yr_); wr(op21Xr_); wr(op0DX_); wr(op0DY_); wr(op0DZ_); wr(op0DF_); wr(op0DL_); wr(op0DU_); wr(op1DX_); wr(op1DY_); wr(op1DZ_); wr(op1DF_); wr(op1DL_); wr(op1DU_); wr(op2DX_); wr(op2DY_); wr(op2DZ_); wr(op2DF_); wr(op2DL_); wr(op2DU_); wr(op03F_); wr(op03L_); wr(op03U_); wr(op03X_); wr(op03Y_); wr(op03Z_); wr(op13F_); wr(op13L_); wr(op13U_); wr(op13X_); wr(op13Y_); wr(op13Z_); wr(op23F_); wr(op23L_); wr(op23U_); wr(op23X_); wr(op23Y_); wr(op23Z_); wr(op0BX_); wr(op0BY_); wr(op0BZ_); wr(op0BS_); wr(op1BX_); wr(op1BY_); wr(op1BZ_); wr(op1BS_); wr(op2BX_); wr(op2BY_); wr(op2BZ_); wr(op2BS_); wr(op14Zr_); wr(op14Xr_); wr(op14Yr_); wr(op14U_); wr(op14F_); wr(op14L_); wr(op14Zrr_); wr(op14Xrr_); wr(op14Yrr_); wr(op0EH_); wr(op0EV_); wr(op0EX_); wr(op0EY_); wr(op08X_); wr(op08Y_); wr(op08Z_); w.u16(op08Ll_); w.u16(op08Lh_); wr(op18X_); wr(op18Y_); wr(op18Z_); wr(op18R_); wr(op18D_); wr(op38X_); wr(op38Y_); wr(op38Z_); wr(op38R_); wr(op38D_); wr(op28X_); wr(op28Y_); wr(op28Z_); wr(op28R_); wr(op1CZ_); wr(op1CY_); wr(op1CX_); wr(op1CXBR_); wr(op1CYBR_); wr(op1CZBR_); wr(op1CX1_); wr(op1CY1_); wr(op1CZ1_); wr(op1CXAR_); wr(op1CYAR_); wr(op1CZAR_); wr(op0FPass_); wr(op0FRamsize_); wr(op2FSize_); wr(op2FUnknown_); w.u8(hirom_?1:0); w.u16(boundary_); }
void Dsp1::deserialize(Reader& r){ waiting4command_=r.b(); firstParam_=r.b(); command_=r.u8(); inCount_=r.u32(); inIndex_=r.u32(); outCount_=r.u32(); outIndex_=r.u32(); r.raw(params_,sizeof(params_)); r.raw(output_,sizeof(output_)); r.raw(matrixA_,sizeof(matrixA_)); r.raw(matrixB_,sizeof(matrixB_)); r.raw(matrixC_,sizeof(matrixC_)); auto rd=[&](){ return int16(r.u16()); }; centreX_=rd(); centreY_=rd(); vOffset_=rd(); vPlaneC_=rd(); vPlaneE_=rd(); sinAas_=rd(); cosAas_=rd(); sinAzs_=rd(); cosAzs_=rd(); sinAZS_=rd(); cosAZS_=rd(); secAzsC1_=rd(); secAzsE1_=rd(); secAzsC2_=rd(); secAzsE2_=rd(); nx_=rd(); ny_=rd(); nz_=rd(); gx_=rd(); gy_=rd(); gz_=rd(); cLes_=rd(); eLes_=rd(); gLes_=rd(); op00Multiplicand_=rd(); op00Multiplier_=rd(); op00Result_=rd(); op20Multiplicand_=rd(); op20Multiplier_=rd(); op20Result_=rd(); op10Coeff_=rd(); op10Exp_=rd(); op10CoeffR_=rd(); op10ExpR_=rd(); op04Angle_=rd(); op04Radius_=r.u16(); op04Sin_=rd(); op04Cos_=rd(); op0CA_=rd(); op0CX1_=rd(); op0CY1_=rd(); op0CX2_=rd(); op0CY2_=rd(); op02FX_=rd(); op02FY_=rd(); op02FZ_=rd(); op02LFE_=rd(); op02LES_=rd(); op02AAS_=r.u16(); op02AZS_=r.u16(); op02VOF_=rd(); op02VVA_=rd(); op02CX_=rd(); op02CY_=rd(); op0AVS_=rd(); op0AA_=rd(); op0AB_=rd(); op0AC_=rd(); op0AD_=rd(); op06X_=rd(); op06Y_=rd(); op06Z_=rd(); op06H_=rd(); op06V_=rd(); op06M_=rd(); op01m_=rd(); op01Zr_=rd(); op01Yr_=rd(); op01Xr_=rd(); op11m_=rd(); op11Zr_=rd(); op11Yr_=rd(); op11Xr_=rd(); op21m_=rd(); op21Zr_=rd(); op21Yr_=rd(); op21Xr_=rd(); op0DX_=rd(); op0DY_=rd(); op0DZ_=rd(); op0DF_=rd(); op0DL_=rd(); op0DU_=rd(); op1DX_=rd(); op1DY_=rd(); op1DZ_=rd(); op1DF_=rd(); op1DL_=rd(); op1DU_=rd(); op2DX_=rd(); op2DY_=rd(); op2DZ_=rd(); op2DF_=rd(); op2DL_=rd(); op2DU_=rd(); op03F_=rd(); op03L_=rd(); op03U_=rd(); op03X_=rd(); op03Y_=rd(); op03Z_=rd(); op13F_=rd(); op13L_=rd(); op13U_=rd(); op13X_=rd(); op13Y_=rd(); op13Z_=rd(); op23F_=rd(); op23L_=rd(); op23U_=rd(); op23X_=rd(); op23Y_=rd(); op23Z_=rd(); op0BX_=rd(); op0BY_=rd(); op0BZ_=rd(); op0BS_=rd(); op1BX_=rd(); op1BY_=rd(); op1BZ_=rd(); op1BS_=rd(); op2BX_=rd(); op2BY_=rd(); op2BZ_=rd(); op2BS_=rd(); op14Zr_=rd(); op14Xr_=rd(); op14Yr_=rd(); op14U_=rd(); op14F_=rd(); op14L_=rd(); op14Zrr_=rd(); op14Xrr_=rd(); op14Yrr_=rd(); op0EH_=rd(); op0EV_=rd(); op0EX_=rd(); op0EY_=rd(); op08X_=rd(); op08Y_=rd(); op08Z_=rd(); op08Ll_=r.u16(); op08Lh_=r.u16(); op18X_=rd(); op18Y_=rd(); op18Z_=rd(); op18R_=rd(); op18D_=rd(); op38X_=rd(); op38Y_=rd(); op38Z_=rd(); op38R_=rd(); op38D_=rd(); op28X_=rd(); op28Y_=rd(); op28Z_=rd(); op28R_=rd(); op1CZ_=rd(); op1CY_=rd(); op1CX_=rd(); op1CXBR_=rd(); op1CYBR_=rd(); op1CZBR_=rd(); op1CX1_=rd(); op1CY1_=rd(); op1CZ1_=rd(); op1CXAR_=rd(); op1CYAR_=rd(); op1CZAR_=rd(); op0FPass_=rd(); op0FRamsize_=rd(); op2FSize_=rd(); op2FUnknown_=rd(); hirom_=(r.u8()!=0); boundary_=r.u16(); }
} // namespace snes
