#include "coprocessor/dsp3.hpp"
#include <cstring>
namespace snes {
const uint16 Dsp3::kDataRom[1024] = {
	0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
	0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001,
	0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100,
	0x0000, 0x000f, 0x0400, 0x0200, 0x0140, 0x0400, 0x0200, 0x0040,
	0x007d, 0x007e, 0x007e, 0x007b, 0x007c, 0x007d, 0x007b, 0x007c,
	0x0002, 0x0020, 0x0030, 0x0000, 0x000d, 0x0019, 0x0026, 0x0032,
	0x003e, 0x004a, 0x0056, 0x0062, 0x006d, 0x0079, 0x0084, 0x008e,
	0x0098, 0x00a2, 0x00ac, 0x00b5, 0x00be, 0x00c6, 0x00ce, 0x00d5,
	0x00dc, 0x00e2, 0x00e7, 0x00ec, 0x00f1, 0x00f5, 0x00f8, 0x00fb,
	0x00fd, 0x00ff, 0x0100, 0x0100, 0x0100, 0x00ff, 0x00fd, 0x00fb,
	0x00f8, 0x00f5, 0x00f1, 0x00ed, 0x00e7, 0x00e2, 0x00dc, 0x00d5,
	0x00ce, 0x00c6, 0x00be, 0x00b5, 0x00ac, 0x00a2, 0x0099, 0x008e,
	0x0084, 0x0079, 0x006e, 0x0062, 0x0056, 0x004a, 0x003e, 0x0032,
	0x0026, 0x0019, 0x000d, 0x0000, 0xfff3, 0xffe7, 0xffdb, 0xffce,
	0xffc2, 0xffb6, 0xffaa, 0xff9e, 0xff93, 0xff87, 0xff7d, 0xff72,
	0xff68, 0xff5e, 0xff54, 0xff4b, 0xff42, 0xff3a, 0xff32, 0xff2b,
	0xff25, 0xff1e, 0xff19, 0xff14, 0xff0f, 0xff0b, 0xff08, 0xff05,
	0xff03, 0xff01, 0xff00, 0xff00, 0xff00, 0xff01, 0xff03, 0xff05,
	0xff08, 0xff0b, 0xff0f, 0xff13, 0xff18, 0xff1e, 0xff24, 0xff2b,
	0xff32, 0xff3a, 0xff42, 0xff4b, 0xff54, 0xff5d, 0xff67, 0xff72,
	0xff7c, 0xff87, 0xff92, 0xff9e, 0xffa9, 0xffb5, 0xffc2, 0xffce,
	0xffda, 0xffe7, 0xfff3, 0x002b, 0x007f, 0x0020, 0x00ff, 0xff00,
	0xffbe, 0x0000, 0x0044, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffc1, 0x0001, 0x0002, 0x0045,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffc5, 0x0003, 0x0004, 0x0005, 0x0047, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffca, 0x0006, 0x0007, 0x0008,
	0x0009, 0x004a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffd0, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x004e, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffd7, 0x000f, 0x0010, 0x0011,
	0x0012, 0x0013, 0x0014, 0x0053, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffdf, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001a, 0x001b,
	0x0059, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffe8, 0x001c, 0x001d, 0x001e,
	0x001f, 0x0020, 0x0021, 0x0022, 0x0023, 0x0060, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xfff2, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a,
	0x002b, 0x002c, 0x0068, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xfffd, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0071,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffc7, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d,
	0x003e, 0x003f, 0x0040, 0x0041, 0x007b, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffd4, 0x0000, 0x0001, 0x0002,
	0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a,
	0x000b, 0x0044, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffe2, 0x000c, 0x000d, 0x000e, 0x000f, 0x0010, 0x0011, 0x0012,
	0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0050, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xfff1, 0x0019, 0x001a, 0x001b,
	0x001c, 0x001d, 0x001e, 0x001f, 0x0020, 0x0021, 0x0022, 0x0023,
	0x0024, 0x0025, 0x0026, 0x005d, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffcb, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d,
	0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035,
	0x006b, 0x0000, 0x0000, 0x0000, 0xffdc, 0x0000, 0x0001, 0x0002,
	0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a,
	0x000b, 0x000c, 0x000d, 0x000e, 0x000f, 0x0044, 0x0000, 0x0000,
	0xffee, 0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016,
	0x0017, 0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e,
	0x001f, 0x0020, 0x0054, 0x0000, 0xffee, 0x0021, 0x0022, 0x0023,
	0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b,
	0x002c, 0x002d, 0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0x0065,
	0xffbe, 0x0000, 0xfeac, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffc1, 0x0001, 0x0002, 0xfead,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffc5, 0x0003, 0x0004, 0x0005, 0xfeaf, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffca, 0x0006, 0x0007, 0x0008,
	0x0009, 0xfeb2, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffd0, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0xfeb6, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffd7, 0x000f, 0x0010, 0x0011,
	0x0012, 0x0013, 0x0014, 0xfebb, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffdf, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001a, 0x001b,
	0xfec1, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffe8, 0x001c, 0x001d, 0x001e,
	0x001f, 0x0020, 0x0021, 0x0022, 0x0023, 0xfec8, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xfff2, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a,
	0x002b, 0x002c, 0xfed0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xfffd, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0xfed9,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffc7, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d,
	0x003e, 0x003f, 0x0040, 0x0041, 0xfee3, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xffd4, 0x0000, 0x0001, 0x0002,
	0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a,
	0x000b, 0xfeac, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffe2, 0x000c, 0x000d, 0x000e, 0x000f, 0x0010, 0x0011, 0x0012,
	0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0xfeb8, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xfff1, 0x0019, 0x001a, 0x001b,
	0x001c, 0x001d, 0x001e, 0x001f, 0x0020, 0x0021, 0x0022, 0x0023,
	0x0024, 0x0025, 0x0026, 0xfec5, 0x0000, 0x0000, 0x0000, 0x0000,
	0xffcb, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d,
	0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035,
	0xfed3, 0x0000, 0x0000, 0x0000, 0xffdc, 0x0000, 0x0001, 0x0002,
	0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a,
	0x000b, 0x000c, 0x000d, 0x000e, 0x000f, 0xfeac, 0x0000, 0x0000,
	0xffee, 0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016,
	0x0017, 0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e,
	0x001f, 0x0020, 0xfebc, 0x0000, 0xffee, 0x0021, 0x0022, 0x0023,
	0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b,
	0x002c, 0x002d, 0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0xfecd,
	0x0154, 0x0218, 0x0110, 0x00b0, 0x00cc, 0x00b0, 0x0088, 0x00b0,
	0x0044, 0x00b0, 0x0000, 0x00b0, 0x00fe, 0xff07, 0x0002, 0x00ff,
	0x00f8, 0x0007, 0x00fe, 0x00ee, 0x07ff, 0x0200, 0x00ef, 0xf800,
	0x0700, 0x00ee, 0xffff, 0xffff, 0xffff, 0x0000, 0x0000, 0x0001,
	0x0001, 0x0001, 0x0001, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff,
	0xffff, 0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000,
	0x0000, 0xffff, 0xffff, 0x0000, 0xffff, 0x0001, 0x0000, 0x0001,
	0x0001, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0x0000,
	0xffff, 0x0001, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000, 0xffff,
	0xffff, 0xffff, 0x0000, 0x0000, 0x0000, 0x0044, 0x0088, 0x00cc,
	0x0110, 0x0154, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};
Dsp3::Dsp3(){ power(); }
auto Dsp3::handles(uint24 address) const -> bool { uint32 b=address>>16; uint32 o=address & 0xFFFF; bool bb=(b>=0x20&&b<=0x3F)||(b>=0xA0&&b<=0xBF); return bb && o>=0x8000; }
void Dsp3::setHandler(Handler h){ handler_=h; }
void Dsp3::callHandler(){ switch(handler_){
  case Handler::Reset: hReset(); break;
  case Handler::Command: hCommand(); break;
  case Handler::TestMemory: hTestMemory(); break;
  case Handler::DumpDataRom: hDumpDataRom(); break;
  case Handler::MemoryDump: hMemoryDump(); break;
  case Handler::Coordinate: hCoordinate(); break;
  case Handler::Convert: hConvert(); break;
  case Handler::ConvertA: hConvertA(); break;
  case Handler::Op03: hOp03(); break;
  case Handler::Op06: hOp06(); break;
  case Handler::Op07: hOp07(); break;
  case Handler::Op07A: hOp07A(); break;
  case Handler::Op07B: hOp07B(); break;
  case Handler::Op10: hOp10(); break;
  case Handler::Op0C: hOp0C(); break;
  case Handler::Op1C: hOp1C(); break;
  case Handler::Op1C_A: hOp1C_A(); break;
  case Handler::Op1C_B: hOp1C_B(); break;
  case Handler::Op1C_C: hOp1C_C(); break;
  case Handler::Op1E: hOp1E(); break;
  case Handler::Op1E_A: hOp1E_A(); break;
  case Handler::Op1E_A1: hOp1E_A1(); break;
  case Handler::Op1E_A2: hOp1E_A2(); break;
  case Handler::Op1E_A3: hOp1E_A3(); break;
  case Handler::Op1E_B: hOp1E_B(); break;
  case Handler::Op1E_B1: hOp1E_B1(); break;
  case Handler::Op1E_C: hOp1E_C(); break;
  case Handler::Op1E_C1: hOp1E_C1(); break;
  case Handler::Op1E_C2: hOp1E_C2(); break;
  case Handler::Decode: hDecode(); break;
  case Handler::DecodeA: hDecodeA(); break;
  case Handler::DecodeSymbols: hDecodeSymbols(); break;
  case Handler::DecodeTree: hDecodeTree(); break;
  case Handler::DecodeData: hDecodeData(); break;
  case Handler::Op3E: hOp3E(); break;
  default: break; } }
void Dsp3::hReset()
{
	dr_ = 0x0080;
	sr_ = 0x0084;
	setHandler(Handler::Command);
}

/*
void Dsp3::DSP3_MemorySize (void)
{
	dr_ = 0x0300;
	setHandler(Handler::Reset);
}
*/

void Dsp3::hTestMemory()
{
	dr_ = 0x0000;
	setHandler(Handler::Reset);
}

void Dsp3::hDumpDataRom()
{
	dr_ = kDataRom[memoryIndex_++];
	if (memoryIndex_ == 1024)
		setHandler(Handler::Reset);
}

void Dsp3::hMemoryDump()
{
	memoryIndex_ = 0;
	setHandler(Handler::DumpDataRom);
	hDumpDataRom();
}

void Dsp3::hOp06()
{
	winLo_ = (uint8) (dr_);
	winHi_ = (uint8) (dr_ >> 8);
	hReset();
}

void Dsp3::hOp03()
{
	int16	Lo  = (uint8) (dr_);
	int16	Hi  = (uint8) (dr_ >> 8);
	int16	Ofs = (winLo_ * Hi << 1) + (Lo << 1);

	dr_ = Ofs >> 1;
	setHandler(Handler::Reset);
}

void Dsp3::hOp07B()
{
	int16	Ofs = (winLo_ * addHi_ << 1) + (addLo_ << 1);

	dr_ = Ofs >> 1;
	setHandler(Handler::Reset);
}

void Dsp3::hOp07A()
{
	int16	Lo = (uint8) (dr_);
	int16	Hi = (uint8) (dr_ >> 8);

	if (Lo & 1)
		Hi += (addLo_ & 1);

	addLo_ += Lo;
	addHi_ += Hi;

	if (addLo_ < 0)
		addLo_ += winLo_;
	else
	if (addLo_ >= winLo_)
		addLo_ -= winLo_;

	if (addHi_ < 0)
		addHi_ += winHi_;
	else
	if (addHi_ >= winHi_)
		addHi_ -= winHi_;

	dr_ = addLo_ | (addHi_ << 8) | ((addHi_ >> 8) & 0xff);
	setHandler(Handler::Op07B);
}

void Dsp3::hOp07()
{
	uint32	dataOfs = ((dr_ << 1) + 0x03b2) & 0x03ff;

	addHi_ = kDataRom[dataOfs];
	addLo_ = kDataRom[dataOfs + 1];

	setHandler(Handler::Op07A);
	sr_ = 0x0080;
}

void Dsp3::hCoordinate()
{
	index_++;

	switch (index_)
	{
		case 3:
			if (dr_ == 0xffff)
				hReset();
			break;

		case 4:
			x_ = dr_;
			break;

		case 5:
			y_ = dr_;
			dr_ = 1;
			break;

		case 6:
			dr_ = x_;
			break;

		case 7:
			dr_ = y_;
			index_ = 0;
			break;
	}
}

void Dsp3::hConvertA()
{
	if (bmIndex_ < 8)
	{
		bitmap_[bmIndex_++] = (uint8) (dr_);
		bitmap_[bmIndex_++] = (uint8) (dr_ >> 8);

		if (bmIndex_ == 8)
		{
			for (int i = 0; i < 8; i++)
			{
				for (int j = 0; j < 8; j++)
				{
					bitplane_[j] <<= 1;
					bitplane_[j] |= (bitmap_[i] >> j) & 1;
				}
			}

			bpIndex_ = 0;
			count_--;
		}
	}

	if (bmIndex_ == 8)
	{
		if (bpIndex_ == 8)
		{
			if (!count_)
				hReset();

			bmIndex_ = 0;
		}
		else
		{
			dr_  = bitplane_[bpIndex_++];
			dr_ |= bitplane_[bpIndex_++] << 8;
		}
	}
}

void Dsp3::hConvert()
{
	count_ = dr_;
	bmIndex_ = 0;
	setHandler(Handler::ConvertA);
}

bool Dsp3::getBits(uint8 Count)
{
	if (!bitsLeft_)
	{
		bitsLeft_ = Count;
		reqBits_ = 0;
	}

	do
	{
		if (!bitCount_)
		{
			sr_ = 0xC0;
			return (false);
		}

		reqBits_ <<= 1;
		if (reqData_ & 0x8000)
			reqBits_++;
		reqData_ <<= 1;

		bitCount_--;
		bitsLeft_--;

	}
	while (bitsLeft_);

	return (true);
}

void Dsp3::hDecodeData()
{
	if (!bitCount_)
	{
		if (sr_ & 0x40)
		{
			reqData_ = dr_;
			bitCount_ += 16;
		}
		else
		{
			sr_ = 0xC0;
			return;
		}
	}

	if (lzCode_ == 1)
	{
		if (!getBits(1))
			return;

		if (reqBits_)
			lzLength_ = 12;
		else
			lzLength_ = 8;

		lzCode_++;
	}

	if (lzCode_ == 2)
	{
		if (!getBits(lzLength_))
			return;

		lzCode_ = 0;
		outwords_--;
		if (!outwords_)
			setHandler(Handler::Reset);

		sr_ = 0x80;
		dr_ = reqBits_;
		return;
	}

	if (baseCode_ == 0xffff)
	{
		if (!getBits(baseLength_))
			return;

		baseCode_ = reqBits_;
	}

	if (!getBits(codeLengths_[baseCode_]))
		return;

	symbol_ = codes_[codeOffsets_[baseCode_] + reqBits_];
	baseCode_ = 0xffff;

	if (symbol_ & 0xff00)
	{
		symbol_ += 0x7f02;
		lzCode_++;
	}
	else
	{
		outwords_--;
		if (!outwords_)
			setHandler(Handler::Reset);
	}

	sr_ = 0x80;
	dr_ = symbol_;
}

void Dsp3::hDecodeTree()
{
	if (!bitCount_)
	{
		reqData_ = dr_;
		bitCount_ += 16;
	}

	if (!baseCodes_)
	{
		getBits(1);

		if (reqBits_)
		{
			baseLength_ = 3;
			baseCodes_ = 8;
		}
		else
		{
			baseLength_ = 2;
			baseCodes_ = 4;
		}
	}

	while (baseCodes_)
	{
		if (!getBits(3))
			return;

		reqBits_++;

		codeLengths_[index_] = (uint8) reqBits_;
		codeOffsets_[index_] = symbol_;
		index_++;

		symbol_ += 1 << reqBits_;
		baseCodes_--;
	}

	baseCode_ = 0xffff;
	lzCode_ = 0;

	setHandler(Handler::DecodeData);
	if (bitCount_)
		hDecodeData();
}

void Dsp3::hDecodeSymbols()
{
	reqData_ = dr_;
	bitCount_ += 16;

	do
	{
		if (bitCommand_ == 0xffff)
		{
			if (!getBits(2))
				return;

			bitCommand_ = reqBits_;
		}

		switch (bitCommand_)
		{
			case 0:
				if (!getBits(9))
					return;
				symbol_ = reqBits_;
				break;

			case 1:
				symbol_++;
				break;

			case 2:
				if (!getBits(1))
					return;
				symbol_ += 2 + reqBits_;
				break;

			case 3:
				if (!getBits(4))
					return;
				symbol_ += 4 + reqBits_;
				break;
		}

		bitCommand_ = 0xffff;

		codes_[index_++] = symbol_;
		codewords_--;

	}
	while (codewords_);

	index_ = 0;
	symbol_ = 0;
	baseCodes_ = 0;

	setHandler(Handler::DecodeTree);
	if (bitCount_)
		hDecodeTree();
}

void Dsp3::hDecodeA()
{
	outwords_ = dr_;
	setHandler(Handler::DecodeSymbols);
	bitCount_ = 0;
	bitsLeft_ = 0;
	symbol_ = 0;
	index_ = 0;
	bitCommand_ = 0xffff;
	sr_ = 0xC0;
}

void Dsp3::hDecode()
{
	codewords_ = dr_;
	setHandler(Handler::DecodeA);
}

// Opcodes 1E/3E bit-perfect to 'dsp3-intro' log
// src: adapted from SD Gundam X/G-Next

void Dsp3::hOp3E()
{
	op3eX_ = (uint8)  (dr_ & 0x00ff);
	op3eY_ = (uint8) ((dr_ & 0xff00) >> 8);

	hOp03();

	terrain_[dr_] = 0x00;
	cost_[dr_]    = 0xff;
	weight_[dr_]  = 0;

	op1eMaxSearchRadius_ = 0;
	op1eMaxPathRadius_   = 0;
}

void Dsp3::hOp1E()
{
	op1eMinRadius_ = (uint8)  (dr_ & 0x00ff);
	op1eMaxRadius_ = (uint8) ((dr_ & 0xff00) >> 8);

	if (op1eMinRadius_ == 0)
		op1eMinRadius_++;

	if (op1eMaxSearchRadius_ >= op1eMinRadius_)
		op1eMinRadius_ = op1eMaxSearchRadius_ + 1;

	if (op1eMaxRadius_ > op1eMaxSearchRadius_)
		op1eMaxSearchRadius_ = op1eMaxRadius_;

	op1eLcvRadius_ = op1eMinRadius_;
	op1eLcvSteps_ = op1eMinRadius_;

	op1eLcvTurns_ = 6;
	op1eTurn_ = 0;

	op1eX_ = op3eX_;
	op1eY_ = op3eY_;

	for (int lcv = 0; lcv < op1eMinRadius_; lcv++)
		op1eD(op1eTurn_, &op1eX_, &op1eY_);

	hOp1E_A();
}

void Dsp3::hOp1E_A()
{
	if (op1eLcvSteps_ == 0)
	{
		op1eLcvRadius_++;

		op1eLcvSteps_ = op1eLcvRadius_;

		op1eX_ = op3eX_;
		op1eY_ = op3eY_;

		for (int lcv = 0; lcv < op1eLcvRadius_; lcv++)
			op1eD(op1eTurn_, &op1eX_, &op1eY_);
	}

	if (op1eLcvRadius_ > op1eMaxRadius_)
	{
		op1eTurn_++;
		op1eLcvTurns_--;

		op1eLcvRadius_ = op1eMinRadius_;
		op1eLcvSteps_ = op1eMinRadius_;

		op1eX_ = op3eX_;
		op1eY_ = op3eY_;

		for (int lcv = 0; lcv < op1eMinRadius_; lcv++)
			op1eD(op1eTurn_, &op1eX_, &op1eY_);
	}

	if (op1eLcvTurns_ == 0)
	{
		dr_ = 0xffff;
		sr_ = 0x0080;
		setHandler(Handler::Op1E_B);
		return;
	}

	dr_ = (uint8) (op1eX_) | ((uint8) (op1eY_) << 8);
	hOp03();

	op1eCell_ = dr_;

	sr_ = 0x0080;
	setHandler(Handler::Op1E_A1);
}

void Dsp3::hOp1E_A1()
{
	sr_ = 0x0084;
	setHandler(Handler::Op1E_A2);
}

void Dsp3::hOp1E_A2()
{
	terrain_[op1eCell_] = (uint8) (dr_ & 0x00ff);

	sr_ = 0x0084;
	setHandler(Handler::Op1E_A3);
}

void Dsp3::hOp1E_A3()
{
	cost_[op1eCell_] = (uint8) (dr_ & 0x00ff);

	if (op1eLcvRadius_ == 1)
	{
		if (terrain_[op1eCell_] & 1)
			weight_[op1eCell_] = 0xff;
		else
			weight_[op1eCell_] = cost_[op1eCell_];
	}
	else
		weight_[op1eCell_] = 0xff;

	op1eD((int16) (op1eTurn_ + 2), &op1eX_, &op1eY_);
	op1eLcvSteps_--;

	sr_ = 0x0080;
	hOp1E_A();
}

void Dsp3::hOp1E_B()
{
	op1eX_ = op3eX_;
	op1eY_ = op3eY_;
	op1eLcvRadius_ = 1;

	op1eSearch_ = 0;

	hOp1E_B1();

	setHandler(Handler::Op1E_C);
}

void Dsp3::hOp1E_B1()
{
	while (op1eLcvRadius_ < op1eMaxRadius_)
	{
		op1eY_--;

		op1eLcvTurns_ = 6;
		op1eTurn_ = 5;

		while (op1eLcvTurns_)
		{
			op1eLcvSteps_ = op1eLcvRadius_;

			while (op1eLcvSteps_)
			{
				op1eD1(op1eTurn_, &op1eX_, &op1eY_);

				if (0 <= op1eY_ && op1eY_ < winHi_ && 0 <= op1eX_ && op1eX_ < winLo_)
				{
					dr_ = (uint8) (op1eX_) | ((uint8) (op1eY_) << 8);
					hOp03();

					op1eCell_ = dr_;
					if (cost_[op1eCell_] < 0x80 && terrain_[op1eCell_] < 0x40)
						hOp1E_B2(); // end cell perimeter
				}

				op1eLcvSteps_--;
			} // end search line

			op1eTurn_--;
			if (op1eTurn_ == 0)
				op1eTurn_ = 6;

			op1eLcvTurns_--;
		} // end circle search

		op1eLcvRadius_++;
	} // end radius search
}

void Dsp3::hOp1E_B2()
{
	int16	cell;
	int16	path;
	int16	x, y;
	int16	lcv_turns;

	path = 0xff;
	lcv_turns = 6;

	while (lcv_turns)
	{
		x = op1eX_;
		y = op1eY_;

		op1eD1(lcv_turns, &x, &y);

		dr_ = (uint8) (x) | ((uint8) (y) << 8);
		hOp03();

		cell = dr_;

		if (0 <= y && y < winHi_ && 0 <= x && x < winLo_)
		{
			if (terrain_[cell] < 0x80 || weight_[cell] == 0)
			{
				if (weight_[cell] < path)
					path = weight_[cell];
			}
		} // end step travel

		lcv_turns--;
	} // end while turns

	if (path != 0xff)
		weight_[op1eCell_] = path + cost_[op1eCell_];
}

void Dsp3::hOp1E_C()
{
	op1eMinRadius_ = (uint8)  (dr_ & 0x00ff);
	op1eMaxRadius_ = (uint8) ((dr_ & 0xff00) >> 8);

	if (op1eMinRadius_ == 0)
		op1eMinRadius_++;

	if (op1eMaxPathRadius_ >= op1eMinRadius_)
		op1eMinRadius_ = op1eMaxPathRadius_ + 1;

	if (op1eMaxRadius_ > op1eMaxPathRadius_)
		op1eMaxPathRadius_ = op1eMaxRadius_;

	op1eLcvRadius_ = op1eMinRadius_;
	op1eLcvSteps_ = op1eMinRadius_;

	op1eLcvTurns_ = 6;
	op1eTurn_ = 0;

	op1eX_ = op3eX_;
	op1eY_ = op3eY_;

	for (int lcv = 0; lcv < op1eMinRadius_; lcv++)
		op1eD(op1eTurn_, &op1eX_, &op1eY_);

	hOp1E_C1();
}

void Dsp3::hOp1E_C1()
{
	if (op1eLcvSteps_ == 0)
	{
		op1eLcvRadius_++;

		op1eLcvSteps_ = op1eLcvRadius_;

		op1eX_ = op3eX_;
		op1eY_ = op3eY_;

		for (int lcv = 0; lcv < op1eLcvRadius_; lcv++)
			op1eD(op1eTurn_, &op1eX_, &op1eY_);
	}

	if (op1eLcvRadius_ > op1eMaxRadius_)
	{
		op1eTurn_++;
		op1eLcvTurns_--;

		op1eLcvRadius_ = op1eMinRadius_;
		op1eLcvSteps_ = op1eMinRadius_;

		op1eX_ = op3eX_;
		op1eY_ = op3eY_;

		for (int lcv = 0; lcv < op1eMinRadius_; lcv++)
			op1eD(op1eTurn_, &op1eX_, &op1eY_);
	}

	if (op1eLcvTurns_ == 0)
	{
		dr_ = 0xffff;
		sr_ = 0x0080;
		setHandler(Handler::Reset);
		return;
	}

	dr_ = (uint8) (op1eX_) | ((uint8) (op1eY_) << 8);
	hOp03();

	op1eCell_ = dr_;

	sr_ = 0x0080;
	setHandler(Handler::Op1E_C2);
}

void Dsp3::hOp1E_C2()
{
	dr_ = weight_[op1eCell_];

	op1eD((int16) (op1eTurn_ + 2), &op1eX_, &op1eY_);
	op1eLcvSteps_--;

	sr_ = 0x0084;
	setHandler(Handler::Op1E_C1);
}

void Dsp3::op1eD(int16 move, int16 *lo, int16 *hi)
{
	uint32	dataOfs = ((move << 1) + 0x03b2) & 0x03ff;
	int16	Lo;
	int16	Hi;

	addHi_ = kDataRom[dataOfs];
	addLo_ = kDataRom[dataOfs + 1];

	Lo = (uint8) (*lo);
	Hi = (uint8) (*hi);

	if (Lo & 1)
		Hi += (addLo_ & 1);

	addLo_ += Lo;
	addHi_ += Hi;

	if (addLo_ < 0)
		addLo_ += winLo_;
	else
	if (addLo_ >= winLo_)
		addLo_ -= winLo_;

	if (addHi_ < 0)
		addHi_ += winHi_;
	else
	if (addHi_ >= winHi_)
		addHi_ -= winHi_;

	*lo = addLo_;
	*hi = addHi_;
}

void Dsp3::op1eD1(int16 move, int16 *lo, int16 *hi)
{
	const uint16	HiAdd[] =
	{
		0x00, 0xFF, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0xFF, 0x00
	};

	const uint16	LoAdd[] =
	{
		0x00, 0x00, 0x01, 0x01, 0x00, 0xFF, 0xFF, 0x00
	};

	int16	Lo;
	int16	Hi;

	if ((*lo) & 1)
		addHi_ = HiAdd[move + 8];
	else
		addHi_ = HiAdd[move + 0];

	addLo_ = LoAdd[move];

	Lo = (uint8) (*lo);
	Hi = (uint8) (*hi);

	if (Lo & 1)
		Hi += (addLo_ & 1);

	addLo_ += Lo;
	addHi_ += Hi;

	*lo = addLo_;
	*hi = addHi_;
}

void Dsp3::hOp10()
{
	if (dr_ == 0xffff)
		hReset();
	else
		// absorb 2 bytes
		dr_ = dr_; // FIXME?
}

/*
void Dsp3::hOp0C_A()
{
	// absorb 2 bytes
	dr_ = 0;
	setHandler(Handler::Reset);
}
*/

void Dsp3::hOp0C()
{
	// absorb 2 bytes
	dr_ = 0;
	//setHandler(Handler::Op0C_A);
	setHandler(Handler::Reset);
}

void Dsp3::hOp1C_C()
{
	// return 2 bytes
	dr_ = 0;
	setHandler(Handler::Reset);
}

void Dsp3::hOp1C_B()
{
	// return 2 bytes
	dr_ = 0;
	setHandler(Handler::Op1C_C);
}

void Dsp3::hOp1C_A()
{
	// absorb 2 bytes
	setHandler(Handler::Op1C_B);
}

void Dsp3::hOp1C()
{
	// absorb 2 bytes
	setHandler(Handler::Op1C_A);
}

void Dsp3::hCommand (void)
{
	if (dr_ < 0x40)
	{
		switch (dr_)
		{
			case 0x02: setHandler(Handler::Coordinate); break;
			case 0x03: setHandler(Handler::Op03);       break;
			case 0x06: setHandler(Handler::Op06);       break;
			case 0x07: setHandler(Handler::Op07);       return;
			case 0x0c: setHandler(Handler::Op0C);       break;
			case 0x0f: setHandler(Handler::TestMemory); break;
			case 0x10: setHandler(Handler::Op10);       break;
			case 0x18: setHandler(Handler::Convert);    break;
			case 0x1c: setHandler(Handler::Op1C);       break;
			case 0x1e: setHandler(Handler::Op1E);       break;
			case 0x1f: setHandler(Handler::MemoryDump); break;
			case 0x38: setHandler(Handler::Decode);     break;
			case 0x3e: setHandler(Handler::Op3E);       break;
			default:
				return;
		}

		sr_ = 0x0080;
		index_ = 0;
	}
}


void Dsp3::power(){ dr_=0x0080; sr_=0x0084; handler_=Handler::Command; memoryIndex_=winLo_=winHi_=addLo_=addHi_=0; codewords_=outwords_=symbol_=bitCount_=index_=0; bitsLeft_=reqBits_=reqData_=bitCommand_=0; baseLength_=0; baseCodes_=baseCode_=0; std::memset(codes_,0,sizeof(codes_)); std::memset(codeLengths_,0,sizeof(codeLengths_)); std::memset(codeOffsets_,0,sizeof(codeOffsets_)); lzCode_=0; lzLength_=0; x_=y_=0; std::memset(bitmap_,0,sizeof(bitmap_)); std::memset(bitplane_,0,sizeof(bitplane_)); bmIndex_=bpIndex_=count_=0; op3eX_=op3eY_=0; std::memset(terrain_,0,sizeof(terrain_)); std::memset(cost_,0,sizeof(cost_)); std::memset(weight_,0,sizeof(weight_)); op1eCell_=op1eTurn_=op1eSearch_=op1eX_=op1eY_=0; op1eMinRadius_=op1eMaxRadius_=op1eMaxSearchRadius_=op1eMaxPathRadius_=op1eLcvRadius_=op1eLcvSteps_=op1eLcvTurns_=0; }
void Dsp3::write(uint24 address, uint8 data){ uint16 a=uint16(address & 0xFFFF); if(a>=0xC000) return; if(sr_ & 0x04){ dr_=(dr_ & 0xFF00) | data; callHandler(); } else { sr_ ^= 0x10; if(sr_ & 0x10) dr_=(dr_ & 0xFF00) | data; else { dr_=(dr_ & 0x00FF) | (uint16(data)<<8); callHandler(); } } }
uint8 Dsp3::read(uint24 address){ uint16 a=uint16(address & 0xFFFF); if(a>=0xC000) return uint8(sr_ & 0xFF); uint8 b; if(sr_ & 0x04){ b=uint8(dr_); callHandler(); } else { sr_ ^=0x10; if(sr_&0x10) b=uint8(dr_); else { b=uint8(dr_>>8); callHandler(); } } return b; }
void Dsp3::serialize(Writer& w) const { w.u16(dr_); w.u16(sr_); w.u16(memoryIndex_); w.u16((uint16)winLo_); w.u16((uint16)winHi_); w.u16((uint16)addLo_); w.u16((uint16)addHi_); w.u16(codewords_); w.u16(outwords_); w.u16(symbol_); w.u16(bitCount_); w.u16(index_); w.raw(codes_,sizeof(codes_)); w.u16(bitsLeft_); w.u16(reqBits_); w.u16(reqData_); w.u16(bitCommand_); w.u8(baseLength_); w.u16(baseCodes_); w.u16(baseCode_); w.raw(codeLengths_,sizeof(codeLengths_)); w.raw(codeOffsets_,sizeof(codeOffsets_)); w.u16(lzCode_); w.u8(lzLength_); w.u16(x_); w.u16(y_); w.raw(bitmap_,sizeof(bitmap_)); w.raw(bitplane_,sizeof(bitplane_)); w.u16(bmIndex_); w.u16(bpIndex_); w.u16(count_); w.u16((uint16)op3eX_); w.u16((uint16)op3eY_); w.raw(terrain_,sizeof(terrain_)); w.raw(cost_,sizeof(cost_)); w.raw(weight_,sizeof(weight_)); w.u16((uint16)op1eCell_); w.u16((uint16)op1eTurn_); w.u16((uint16)op1eSearch_); w.u16((uint16)op1eX_); w.u16((uint16)op1eY_); w.u16((uint16)op1eMinRadius_); w.u16((uint16)op1eMaxRadius_); w.u16((uint16)op1eMaxSearchRadius_); w.u16((uint16)op1eMaxPathRadius_); w.u16((uint16)op1eLcvRadius_); w.u16((uint16)op1eLcvSteps_); w.u16((uint16)op1eLcvTurns_); w.u8((uint8)handler_); }
void Dsp3::deserialize(Reader& r){ dr_=r.u16(); sr_=r.u16(); memoryIndex_=r.u16(); winLo_=int16(r.u16()); winHi_=int16(r.u16()); addLo_=int16(r.u16()); addHi_=int16(r.u16()); codewords_=r.u16(); outwords_=r.u16(); symbol_=r.u16(); bitCount_=r.u16(); index_=r.u16(); r.raw(codes_,sizeof(codes_)); bitsLeft_=r.u16(); reqBits_=r.u16(); reqData_=r.u16(); bitCommand_=r.u16(); baseLength_=r.u8(); baseCodes_=r.u16(); baseCode_=r.u16(); r.raw(codeLengths_,sizeof(codeLengths_)); r.raw(codeOffsets_,sizeof(codeOffsets_)); lzCode_=r.u16(); lzLength_=r.u8(); x_=r.u16(); y_=r.u16(); r.raw(bitmap_,sizeof(bitmap_)); r.raw(bitplane_,sizeof(bitplane_)); bmIndex_=r.u16(); bpIndex_=r.u16(); count_=r.u16(); op3eX_=int16(r.u16()); op3eY_=int16(r.u16()); r.raw(terrain_,sizeof(terrain_)); r.raw(cost_,sizeof(cost_)); r.raw(weight_,sizeof(weight_)); op1eCell_=int16(r.u16()); op1eTurn_=int16(r.u16()); op1eSearch_=int16(r.u16()); op1eX_=int16(r.u16()); op1eY_=int16(r.u16()); op1eMinRadius_=int16(r.u16()); op1eMaxRadius_=int16(r.u16()); op1eMaxSearchRadius_=int16(r.u16()); op1eMaxPathRadius_=int16(r.u16()); op1eLcvRadius_=int16(r.u16()); op1eLcvSteps_=int16(r.u16()); op1eLcvTurns_=int16(r.u16()); handler_=(Handler)r.u8(); }
} // namespace snes
