#include "coprocessor/dsp4.hpp"
#include <cstring>
#include <algorithm>
namespace snes {
static inline uint16 rd16(const uint8* p){ return uint16(p[0]) | (uint16(p[1])<<8); }
static inline void wr16(uint8* p, uint16 v){ p[0]=uint8(v); p[1]=uint8(v>>8); }
static inline int16 rd16s(const uint8* p){ return int16(rd16(p)); }
#define SEX78(a) (int32(int16(a))<<8)
#define SEX16(a) (int32(int16(a))<<16)


int16 Dsp4::readWord()
{
	int16	out;

	out = rd16(params_ + inIndex_);
	inIndex_ += 2;

	return (out);
}

int32 Dsp4::readDword()
{
	int32 lo = rd16(params_ + inIndex_);
	int32 hi = rd16(params_ + inIndex_ + 2);
	int32 out = lo | (hi << 16);
	inIndex_ += 4;
	return out;
}

int16 Dsp4::inv(int16 value)
{
	// Attention: This lookup table is not verified
	const uint16	div_lut[64] =
	{
		0x0000, 0x8000, 0x4000, 0x2aaa, 0x2000, 0x1999, 0x1555, 0x1249,
		0x1000, 0x0e38, 0x0ccc, 0x0ba2, 0x0aaa, 0x09d8, 0x0924, 0x0888,
		0x0800, 0x0787, 0x071c, 0x06bc, 0x0666, 0x0618, 0x05d1, 0x0590,
		0x0555, 0x051e, 0x04ec, 0x04bd, 0x0492, 0x0469, 0x0444, 0x0421,
		0x0400, 0x03e0, 0x03c3, 0x03a8, 0x038e, 0x0375, 0x035e, 0x0348,
		0x0333, 0x031f, 0x030c, 0x02fa, 0x02e8, 0x02d8, 0x02c8, 0x02b9,
		0x02aa, 0x029c, 0x028f, 0x0282, 0x0276, 0x026a, 0x025e, 0x0253,
		0x0249, 0x023e, 0x0234, 0x022b, 0x0222, 0x0219, 0x0210, 0x0208
	};

	// saturate bounds
	if (value < 0)
		value = 0;
	if (value > 63)
		value = 63;

	return (div_lut[value]);
}

void Dsp4::multiply(int16 Multiplicand, int16 Multiplier, int32 *Product)
{
	*Product = (Multiplicand * Multiplier << 1) >> 1;
}

void Dsp4::op01()
{
	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
		case 3: goto resume3; break;
	}

	////////////////////////////////////////////////////
	// process initial inputs

	// sort inputs
	worldY_           = readDword();
	polyBottom_[0][0] = readWord();
	polyTop_[0][0]    = readWord();
	polyCx_[1][0]     = readWord();
	viewportBottom_   = readWord();
	worldX_           = readDword();
	polyCx_[0][0]     = readWord();
	polyPtr_[0][0]    = readWord();
	worldYofs_        = readWord();
	worldDy_          = readDword();
	worldDx_          = readDword();
	distance_          = readWord();
	readWord(); // 0x0000
	worldXenv_        = readDword();
	worldDdy_         = readWord();
	worldDdx_         = readWord();
	viewYofsenv_      = readWord();

	// initial (x, y, offset) at starting raster line
	viewX1_         = (worldX_ + worldXenv_) >> 16;
	viewY1_         = worldY_ >> 16;
	viewXofs1_      = worldX_ >> 16;
	viewYofs1_      = worldYofs_;
	viewTurnoffX_  = 0;
	viewTurnoffDx_ = 0;

	// first raster line
	polyRaster_[0][0] = polyBottom_[0][0];

	do
	{
		////////////////////////////////////////////////////
		// process one iteration of projection

		// perspective projection of world (x, y, scroll) points
		// based on the current projection lines
		viewX2_    = (((worldX_ + worldXenv_) >> 16) * distance_ >> 15) + (viewTurnoffX_ * distance_ >> 15);
		viewY2_    = (worldY_ >> 16) * distance_ >> 15;
		viewXofs2_ = viewX2_;
		viewYofs2_ = (worldYofs_ * distance_ >> 15) + polyBottom_[0][0] - viewY2_;

		// 1. World x-location before transformation
		// 2. Viewer x-position at the next
		// 3. World y-location before perspective projection
		// 4. Viewer y-position below the horizon
		// 5. Number of raster lines drawn in this iteration
		{ outCount_=0; outIndex_=0; }
		writeWord((worldX_ + worldXenv_) >> 16);
		writeWord(viewX2_);
		writeWord(worldY_ >> 16);
		writeWord(viewY2_);

		//////////////////////////////////////////////////////

		// SR = 0x00

		// determine # of raster lines used
		segments_ = polyRaster_[0][0] - viewY2_;

		// prevent overdraw
		if (viewY2_ >= polyRaster_[0][0])
			segments_ = 0;
		else
			polyRaster_[0][0] = viewY2_;

		// don't draw outside the window
		if (viewY2_ < polyTop_[0][0])
		{
			segments_ = 0;

			// flush remaining raster lines
			if (viewY1_ >= polyTop_[0][0])
				segments_ = viewY1_ - polyTop_[0][0];
		}

		// SR = 0x80

		writeWord(segments_);

		//////////////////////////////////////////////////////

		// scan next command if no SR check needed
		if (segments_)
		{
			int32	px_dx, py_dy;
			int32	x_scroll, y_scroll;

			// SR = 0x00

			// linear interpolation (lerp) between projected points
			px_dx = (viewXofs2_ - viewXofs1_) * inv(segments_) << 1;
			py_dy = (viewYofs2_ - viewYofs1_) * inv(segments_) << 1;

			// starting step values
			x_scroll = SEX16(polyCx_[0][0] + viewXofs1_);
			y_scroll = SEX16(-viewportBottom_ + viewYofs1_ + viewYofsenv_ + polyCx_[1][0] - worldYofs_);

			// SR = 0x80

			// rasterize line
			for (lcv_ = 0; lcv_ < segments_; lcv_++)
			{
				// 1. HDMA memory pointer (bg1)
				// 2. vertical scroll offset ($210E)
				// 3. horizontal scroll offset ($210D)
				writeWord(polyPtr_[0][0]);
				writeWord((y_scroll + 0x8000) >> 16);
				writeWord((x_scroll + 0x8000) >> 16);

				// update memory address
				polyPtr_[0][0] -= 4;

				// update screen values
				x_scroll += px_dx;
				y_scroll += py_dy;
			}
		}

		////////////////////////////////////////////////////
		// Post-update

		// update new viewer (x, y, scroll) to last raster line drawn
		viewX1_    = viewX2_;
		viewY1_    = viewY2_;
		viewXofs1_ = viewXofs2_;
		viewYofs1_ = viewYofs2_;

		// add deltas for projection lines
		worldDx_ += SEX78(worldDdx_);
		worldDy_ += SEX78(worldDdy_);

		// update projection lines
		worldX_ += (worldDx_ + worldXenv_);
		worldY_ += worldDy_;

		// update road turnoff position
		viewTurnoffX_ += viewTurnoffDx_;

		////////////////////////////////////////////////////
		// command check

		// scan next command
		inCount_ = 2;
		{ inIndex_=0; logic_=1; return; }
		
		resume1:

		// check for termination
		distance_ = readWord();
		if (distance_ == -0x8000)
			break;

		// road turnoff
		if ((uint16) distance_ == 0x8001)
		{
			inCount_ = 6;
			{ inIndex_=0; logic_=2; return; }

			resume2:

			distance_        = readWord();
			viewTurnoffX_  = readWord();
			viewTurnoffDx_ = readWord();

			// factor in new changes
			viewX1_    += (viewTurnoffX_ * distance_ >> 15);
			viewXofs1_ += (viewTurnoffX_ * distance_ >> 15);

			// update stepping values
			viewTurnoffX_ += viewTurnoffDx_;

			inCount_ = 2;
			{ inIndex_=0; logic_=1; return; }
		}

		// already have 2 bytes read
		inCount_ = 6;
		{ inIndex_=0; logic_=3; return; }

		resume3:

		// inspect inputs
		worldDdy_    = readWord();
		worldDdx_    = readWord();
		viewYofsenv_ = readWord();

		// no envelope here
		worldXenv_ = 0;
	}
	while (1);

	// terminate op
	waiting4command_ = true;
}

void Dsp4::op03()
{
	oamRowMax_ = 33;
	memset(oamRow_, 0, 64);
}

void Dsp4::op05()
{
	oamIndex_ = 0;
	oamBits_ = 0;
	memset(oamAttr_, 0, 32);
	spriteCount_ = 0;
}

void Dsp4::op06()
{
	{ outCount_=0; outIndex_=0; }
	{ for(int _i=0;_i<16;_i++) writeWord(oamAttr_[_i]); }
}

void Dsp4::op07()
{
	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
	}

	////////////////////////////////////////////////////
	// sort inputs

	worldY_           = readDword();
	polyBottom_[0][0] = readWord();
	polyTop_[0][0]    = readWord();
	polyCx_[1][0]     = readWord();
	viewportBottom_   = readWord();
	worldX_           = readDword();
	polyCx_[0][0]     = readWord();
	polyPtr_[0][0]    = readWord();
	worldYofs_        = readWord();
	distance_          = readWord();
	viewY2_           = readWord();
	viewDy_           = readWord() * distance_ >> 15;
	viewX2_           = readWord();
	viewDx_           = readWord() * distance_ >> 15;
	viewYofsenv_      = readWord();

	// initial (x, y, offset) at starting raster line
	viewX1_    = worldX_ >> 16;
	viewY1_    = worldY_ >> 16;
	viewXofs1_ = viewX1_;
	viewYofs1_ = worldYofs_;

	// first raster line
	polyRaster_[0][0] = polyBottom_[0][0];

	do
	{
		////////////////////////////////////////////////////
		// process one iteration of projection

		// add shaping
		viewX2_ += viewDx_;
		viewY2_ += viewDy_;

		// vertical scroll calculation
		viewXofs2_ = viewX2_;
		viewYofs2_ = (worldYofs_ * distance_ >> 15) + polyBottom_[0][0] - viewY2_;

		// 1. Viewer x-position at the next
		// 2. Viewer y-position below the horizon
		// 3. Number of raster lines drawn in this iteration
		{ outCount_=0; outIndex_=0; }
		writeWord(viewX2_);
		writeWord(viewY2_);

		//////////////////////////////////////////////////////

		// SR = 0x00

		// determine # of raster lines used
		segments_ = viewY1_ - viewY2_;

		// prevent overdraw
		if (viewY2_ >= polyRaster_[0][0])
			segments_ = 0;
		else
			polyRaster_[0][0] = viewY2_;

		// don't draw outside the window
		if (viewY2_ < polyTop_[0][0])
		{
			segments_ = 0;

			// flush remaining raster lines
			if (viewY1_ >= polyTop_[0][0])
				segments_ = viewY1_ - polyTop_[0][0];
		}

		// SR = 0x80

		writeWord(segments_);

		//////////////////////////////////////////////////////

		// scan next command if no SR check needed
		if (segments_)
		{
			int32	px_dx, py_dy;
			int32	x_scroll, y_scroll;

			// SR = 0x00

			// linear interpolation (lerp) between projected points
			px_dx = (viewXofs2_ - viewXofs1_) * inv(segments_) << 1;
			py_dy = (viewYofs2_ - viewYofs1_) * inv(segments_) << 1;

			// starting step values
			x_scroll = SEX16(polyCx_[0][0] + viewXofs1_);
			y_scroll = SEX16(-viewportBottom_ + viewYofs1_ + viewYofsenv_ + polyCx_[1][0] - worldYofs_);

			// SR = 0x80

			// rasterize line
			for (lcv_ = 0; lcv_ < segments_; lcv_++)
			{
				// 1. HDMA memory pointer (bg2)
				// 2. vertical scroll offset ($2110)
				// 3. horizontal scroll offset ($210F)
				writeWord(polyPtr_[0][0]);
				writeWord((y_scroll + 0x8000) >> 16);
				writeWord((x_scroll + 0x8000) >> 16);

				// update memory address
				polyPtr_[0][0] -= 4;

				// update screen values
				x_scroll += px_dx;
				y_scroll += py_dy;
			}
		}

		/////////////////////////////////////////////////////
		// Post-update

		// update new viewer (x, y, scroll) to last raster line drawn
		viewX1_    = viewX2_;
		viewY1_    = viewY2_;
		viewXofs1_ = viewXofs2_;
		viewYofs1_ = viewYofs2_;

		////////////////////////////////////////////////////
		// command check

		// scan next command
		inCount_ = 2;
		{ inIndex_=0; logic_=1; return; }

		resume1:

		// check for opcode termination
		distance_ = readWord();
		if (distance_ == -0x8000)
			break;

		// already have 2 bytes in queue
		inCount_ = 10;
		{ inIndex_=0; logic_=2; return; }

		resume2:

		// inspect inputs
		viewY2_      = readWord();
		viewDy_      = readWord() * distance_ >> 15;
		viewX2_      = readWord();
		viewDx_      = readWord() * distance_ >> 15;
		viewYofsenv_ = readWord();
	}
	while (1);

	waiting4command_ = true;
}

void Dsp4::op08()
{
	int16	win_left, win_right;
	int16	view_x[2], view_y[2];
	int16	envelope[2][2];

	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
	}

	////////////////////////////////////////////////////
	// process initial inputs for two polygons

	// clip values
	polyClipRt_[0][0] = readWord();
	polyClipRt_[0][1] = readWord();
	polyClipRt_[1][0] = readWord();
	polyClipRt_[1][1] = readWord();

	polyClipLf_[0][0] = readWord();
	polyClipLf_[0][1] = readWord();
	polyClipLf_[1][0] = readWord();
	polyClipLf_[1][1] = readWord();

	// unknown (constant) (ex. 1P/2P = $00A6, $00A6, $00A6, $00A6)
	readWord();
	readWord();
	readWord();
	readWord();

	// unknown (constant) (ex. 1P/2P = $00A5, $00A5, $00A7, $00A7)
	readWord();
	readWord();
	readWord();
	readWord();

	// polygon centering (left, right)
	polyCx_[0][0] = readWord();
	polyCx_[0][1] = readWord();
	polyCx_[1][0] = readWord();
	polyCx_[1][1] = readWord();

	// HDMA pointer locations
	polyPtr_[0][0] = readWord();
	polyPtr_[0][1] = readWord();
	polyPtr_[1][0] = readWord();
	polyPtr_[1][1] = readWord();

	// starting raster line below the horizon
	polyBottom_[0][0] = readWord();
	polyBottom_[0][1] = readWord();
	polyBottom_[1][0] = readWord();
	polyBottom_[1][1] = readWord();

	// top boundary line to clip
	polyTop_[0][0] = readWord();
	polyTop_[0][1] = readWord();
	polyTop_[1][0] = readWord();
	polyTop_[1][1] = readWord();

	// unknown
	// (ex. 1P = $2FC8, $0034, $FF5C, $0035)
	//
	// (ex. 2P = $3178, $0034, $FFCC, $0035)
	// (ex. 2P = $2FC8, $0034, $FFCC, $0035)
	readWord();
	readWord();
	readWord();
	readWord();

	// look at guidelines for both polygon shapes
	distance_ = readWord();
	view_x[0] = readWord();
	view_y[0] = readWord();
	view_x[1] = readWord();
	view_y[1] = readWord();

	// envelope shaping guidelines (one frame only)
	envelope[0][0] = readWord();
	envelope[0][1] = readWord();
	envelope[1][0] = readWord();
	envelope[1][1] = readWord();

	// starting base values to project from
	polyStart_[0] = view_x[0];
	polyStart_[1] = view_x[1];

	// starting raster lines to begin drawing
	polyRaster_[0][0] = view_y[0];
	polyRaster_[0][1] = view_y[0];
	polyRaster_[1][0] = view_y[1];
	polyRaster_[1][1] = view_y[1];

	// starting distances
	polyPlane_[0] = distance_;
	polyPlane_[1] = distance_;

	// SR = 0x00

	// re-center coordinates
	win_left  = polyCx_[0][0] - view_x[0] + envelope[0][0];
	win_right = polyCx_[0][1] - view_x[0] + envelope[0][1];

	// saturate offscreen data for polygon #1
	if (win_left  < polyClipLf_[0][0])
		win_left  = polyClipLf_[0][0];
	if (win_left  > polyClipRt_[0][0])
		win_left  = polyClipRt_[0][0];
	if (win_right < polyClipLf_[0][1])
		win_right = polyClipLf_[0][1];
	if (win_right > polyClipRt_[0][1])
		win_right = polyClipRt_[0][1];

	// SR = 0x80

	// initial output for polygon #1
	{ outCount_=0; outIndex_=0; }
	writeByte(win_left  & 0xff);
	writeByte(win_right & 0xff);

	do
	{
		int16	polygon;

		////////////////////////////////////////////////////
		// command check

		// scan next command
		inCount_ = 2;
		{ inIndex_=0; logic_=1; return; }

		resume1:

		// terminate op
		distance_ = readWord();
		if (distance_ == -0x8000)
			break;

		// already have 2 bytes in queue
		inCount_ = 16;
		{ inIndex_=0; logic_=2; return; }

		resume2:

		// look at guidelines for both polygon shapes
		view_x[0] = readWord();
		view_y[0] = readWord();
		view_x[1] = readWord();
		view_y[1] = readWord();

		// envelope shaping guidelines (one frame only)
		envelope[0][0] = readWord();
		envelope[0][1] = readWord();
		envelope[1][0] = readWord();
		envelope[1][1] = readWord();

		////////////////////////////////////////////////////
		// projection begins

		// init
		{ outCount_=0; outIndex_=0; }

		//////////////////////////////////////////////
		// solid polygon renderer - 2 shapes

		for (polygon = 0; polygon < 2; polygon++)
		{
			int32	left_inc, right_inc;
			int16	x1_final, x2_final;
			int16	env[2][2];
			int16	poly;

			// SR = 0x00

			// # raster lines to draw
			segments_ = polyRaster_[polygon][0] - view_y[polygon];

			// prevent overdraw
			if (segments_ > 0)
			{
				// bump drawing cursor
				polyRaster_[polygon][0] = view_y[polygon];
				polyRaster_[polygon][1] = view_y[polygon];
			}
			else
				segments_ = 0;

			// don't draw outside the window
			if (view_y[polygon] < polyTop_[polygon][0])
			{
				segments_ = 0;

				// flush remaining raster lines
				if (view_y[polygon] >= polyTop_[polygon][0])
					segments_ = view_y[polygon] - polyTop_[polygon][0];
			}

			// SR = 0x80

			// tell user how many raster structures to read in
			writeWord(segments_);

			// normal parameters
			poly = polygon;

			/////////////////////////////////////////////////////

			// scan next command if no SR check needed
			if (segments_)
			{
				int32	w_left, w_right;

				// road turnoff selection
				if ((uint16) envelope[polygon][0] == (uint16) 0xc001)
					poly = 1;
				else
				if (envelope[polygon][1] == 0x3fff)
					poly = 1;

				///////////////////////////////////////////////
				// left side of polygon

				// perspective correction on additional shaping parameters
				env[0][0] = envelope[polygon][0] * polyPlane_[poly] >> 15;
				env[0][1] = envelope[polygon][0] * distance_ >> 15;

				// project new shapes (left side)
				x1_final = view_x[poly] + env[0][0];
				x2_final = polyStart_[poly] + env[0][1];

				// interpolate between projected points with shaping
				left_inc = (x2_final - x1_final) * inv(segments_) << 1;
				if (segments_ == 1)
					left_inc = -left_inc;

				///////////////////////////////////////////////
				// right side of polygon

				// perspective correction on additional shaping parameters
				env[1][0] = envelope[polygon][1] * polyPlane_[poly] >> 15;
				env[1][1] = envelope[polygon][1] * distance_ >> 15;

				// project new shapes (right side)
				x1_final = view_x[poly] + env[1][0];
				x2_final = polyStart_[poly] + env[1][1];

				// interpolate between projected points with shaping
				right_inc = (x2_final - x1_final) * inv(segments_) << 1;
				if (segments_ == 1)
					right_inc = -right_inc;

				///////////////////////////////////////////////
				// update each point on the line

				w_left  = SEX16(polyCx_[polygon][0] - polyStart_[poly] + env[0][0]);
				w_right = SEX16(polyCx_[polygon][1] - polyStart_[poly] + env[1][0]);

				// update distance drawn into world
				polyPlane_[polygon] = distance_;

				// rasterize line
				for (lcv_ = 0; lcv_ < segments_; lcv_++)
				{
					int16	x_left, x_right;

					// project new coordinates
					w_left  += left_inc;
					w_right += right_inc;

					// grab integer portion, drop fraction (no rounding)
					x_left  = w_left  >> 16;
					x_right = w_right >> 16;

					// saturate offscreen data
					if (x_left  < polyClipLf_[polygon][0])
						x_left  = polyClipLf_[polygon][0];
					if (x_left  > polyClipRt_[polygon][0])
						x_left  = polyClipRt_[polygon][0];
					if (x_right < polyClipLf_[polygon][1])
						x_right = polyClipLf_[polygon][1];
					if (x_right > polyClipRt_[polygon][1])
						x_right = polyClipRt_[polygon][1];

					// 1. HDMA memory pointer
					// 2. Left window position ($2126/$2128)
					// 3. Right window position ($2127/$2129)
					writeWord(polyPtr_[polygon][0]);
					writeByte(x_left  & 0xff);
					writeByte(x_right & 0xff);

					// update memory pointers
					polyPtr_[polygon][0] -= 4;
					polyPtr_[polygon][1] -= 4;
				} // end rasterize line
			}

			////////////////////////////////////////////////
			// Post-update

			// new projection spot to continue rasterizing from
			polyStart_[polygon] = view_x[poly];
		} // end polygon rasterizer
	}
	while (1);

	// unknown output
	{ outCount_=0; outIndex_=0; }
	writeWord(0);

	waiting4command_ = true;
}

void Dsp4::op09()
{
	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
		case 3: goto resume3; break;
		case 4: goto resume4; break;
		case 5: goto resume5; break;
		case 6: goto resume6; break;
	}

	////////////////////////////////////////////////////
	// process initial inputs

	// grab screen information
	viewportCx_     = readWord();
	viewportCy_     = readWord();
	readWord(); // 0x0000
	viewportLeft_   = readWord();
	viewportRight_  = readWord();
	viewportTop_    = readWord();
	viewportBottom_ = readWord();

	// starting raster line below the horizon
	polyBottom_[0][0] = viewportBottom_ - viewportCy_;
	polyRaster_[0][0] = 0x100;

	do
	{
		////////////////////////////////////////////////////
		// check for new sprites

		inCount_ = 4;
		{ inIndex_=0; logic_=1; return; }

		resume1:

		////////////////////////////////////////////////
		// raster overdraw check

		raster_ = readWord();

		// continue updating the raster line where overdraw begins
		if (raster_ < polyRaster_[0][0])
		{
			spriteClipy_ = viewportBottom_ - (polyBottom_[0][0] - raster_);
			polyRaster_[0][0] = raster_;
		}

		/////////////////////////////////////////////////
		// identify sprite

		// op termination
		distance_ = readWord();
		if (distance_ == -0x8000)
			goto terminate;

		// no sprite
		if (distance_ == 0x0000)
			continue;

		////////////////////////////////////////////////////
		// process projection information

		// vehicle sprite
		if ((uint16) distance_ == 0x9000)
		{
			int16	car_left, car_right, car_back;
			int16	impact_left, impact_back;
			int16	world_spx, world_spy;
			int16	view_spx, view_spy;
			uint16	energy;

			// we already have 4 bytes we want
			inCount_ = 14;
			{ inIndex_=0; logic_=2; return; }

			resume2:

			// filter inputs
			energy        = readWord();
			impact_back   = readWord();
			car_back      = readWord();
			impact_left   = readWord();
			car_left      = readWord();
			distance_ = readWord();
			car_right     = readWord();

			// calculate car's world (x, y) values
			world_spx = car_right - car_left;
			world_spy = car_back;

			// add in collision vector [needs bit-twiddling]
			world_spx -= energy * (impact_left - car_left) >> 16;
			world_spy -= energy * (car_back - impact_back) >> 16;

			// perspective correction for world (x, y)
			view_spx = world_spx * distance_ >> 15;
			view_spy = world_spy * distance_ >> 15;

			// convert to screen values
			spriteX_ = viewportCx_ + view_spx;
			spriteY_ = viewportBottom_ - (polyBottom_[0][0] - view_spy);

			// make the car's (x)-coordinate available
			{ outCount_=0; outIndex_=0; }
			writeWord(world_spx);

			// grab a few remaining vehicle values
			inCount_ = 4;
			{ inIndex_=0; logic_=3; return; }

			resume3:

			// add vertical lift factor
			spriteY_ += readWord();
		}
		// terrain sprite
		else
		{
			int16	world_spx, world_spy;
			int16	view_spx, view_spy;

			// we already have 4 bytes we want
			inCount_ = 10;
			{ inIndex_=0; logic_=4; return; }

			resume4:

			// sort loop inputs
			polyCx_[0][0]     = readWord();
			polyRaster_[0][1] = readWord();
			world_spx              = readWord();
			world_spy              = readWord();

			// compute base raster line from the bottom
			segments_ = polyBottom_[0][0] - raster_;

			// perspective correction for world (x, y)
			view_spx = world_spx * distance_ >> 15;
			view_spy = world_spy * distance_ >> 15;

			// convert to screen values
			spriteX_ = viewportCx_ + view_spx - polyCx_[0][0];
			spriteY_ = viewportBottom_ - segments_ + view_spy;
		}

		// default sprite size: 16x16
		spriteSize_ = 1;
		spriteAttr_ = readWord();

		////////////////////////////////////////////////////
		// convert tile data to SNES OAM format

		do
		{
			int16	sp_x, sp_y, sp_attr, sp_dattr;
			int16	sp_dx, sp_dy;
			int16	pixels;
			uint16	header;
			bool	draw;

			inCount_ = 2;
			{ inIndex_=0; logic_=5; return; }

			resume5:

			draw = true;

			// opcode termination
			raster_ = readWord();
			if (raster_ == -0x8000)
				goto terminate;

			// stop code
			if (raster_ == 0x0000 && !spriteSize_)
				break;

			// toggle sprite size
			if (raster_ == 0x0000)
			{
				spriteSize_ = !spriteSize_;
				continue;
			}

			// check for valid sprite header
			header = raster_;
			header >>= 8;
			if (header != 0x20 &&
				header != 0x2e && // This is for attractor sprite
				header != 0x40 &&
				header != 0x60 &&
				header != 0xa0 &&
				header != 0xc0 &&
				header != 0xe0)
				break;

			// read in rest of sprite data
			inCount_ = 4;
			{ inIndex_=0; logic_=6; return; }

			resume6:

			draw = true;

			/////////////////////////////////////
			// process tile data

			// sprite deltas
			sp_dattr = raster_;
			sp_dy = readWord();
			sp_dx = readWord();

			// update coordinates to screen space
			sp_x = spriteX_ + sp_dx;
			sp_y = spriteY_ + sp_dy;

			// update sprite nametable/attribute information
			sp_attr = spriteAttr_ + sp_dattr;

			// allow partially visibile tiles
			pixels = spriteSize_ ? 15 : 7;

			{ outCount_=0; outIndex_=0; }

			// transparent tile to clip off parts of a sprite (overdraw)
			if (spriteClipy_ - pixels <= sp_y && sp_y <= spriteClipy_ && sp_x >= viewportLeft_ - pixels && sp_x <= viewportRight_ && spriteClipy_ >= viewportTop_ - pixels && spriteClipy_ <= viewportBottom_)
				op0B(&draw, sp_x, spriteClipy_, 0x00EE, spriteSize_, 0);

			// normal sprite tile
			if (sp_x >= viewportLeft_ - pixels && sp_x <= viewportRight_ && sp_y >= viewportTop_ - pixels && sp_y <= viewportBottom_ && sp_y <= spriteClipy_)
				op0B(&draw, sp_x, sp_y, sp_attr, spriteSize_, 0);

			// no following OAM data
			op0B(&draw, 0, 0x0100, 0, 0, 1);
		}
		while (1);
	}
	while (1);

	terminate:
	waiting4command_ = true;
}

void Dsp4::op0A(int16 n2, int16 *o1, int16 *o2, int16 *o3, int16 *o4)
{
	const uint16	OP0A_Values[16] =
	{
		0x0000, 0x0030, 0x0060, 0x0090, 0x00c0, 0x00f0, 0x0120, 0x0150,
		0xfe80, 0xfeb0, 0xfee0, 0xff10, 0xff40, 0xff70, 0xffa0, 0xffd0
	};

	*o4 = OP0A_Values[(n2 & 0x000f)];
	*o3 = OP0A_Values[(n2 & 0x00f0) >> 4];
	*o2 = OP0A_Values[(n2 & 0x0f00) >> 8];
	*o1 = OP0A_Values[(n2 & 0xf000) >> 12];
}

void Dsp4::op0B(bool *draw, int16 sp_x, int16 sp_y, int16 sp_attr, bool size, bool stop)
{
	int16	Row1, Row2;

	// SR = 0x00

	// align to nearest 8-pixel row
	Row1 = (sp_y >> 3) & 0x1f;
	Row2 = (Row1 + 1)  & 0x1f;

	// check boundaries
	if (!((sp_y < 0) || ((sp_y & 0x01ff) < 0x00eb)))
		*draw = 0;

	if (size)
	{
		if (oamRow_[Row1] + 1 >= oamRowMax_)
			*draw = 0;
		if (oamRow_[Row2] + 1 >= oamRowMax_)
			*draw = 0;
	}
	else
	{
		if (oamRow_[Row1] >= oamRowMax_)
			*draw = 0;
	}

	// emulator fail-safe (unknown if this really exists)
	if (spriteCount_ >= 128)
		*draw = 0;

	// SR = 0x80

	if (*draw)
	{
		// Row tiles
		if (size)
		{
			oamRow_[Row1] += 2;
			oamRow_[Row2] += 2;
		}
		else
			oamRow_[Row1]++;

		// yield OAM output
		writeWord(1);

		// pack OAM data: x, y, name, attr
		writeByte(sp_x & 0xff);
		writeByte(sp_y & 0xff);
		writeWord(sp_attr);

		spriteCount_++;

		// OAM: size, msb data
		// save post-oam table data for future retrieval
		oamAttr_[oamIndex_] |= ((sp_x < 0 || sp_x > 255) << oamBits_);
		oamBits_++;

		oamAttr_[oamIndex_] |= (size << oamBits_);
		oamBits_++;

		// move to next byte in buffer
		if (oamBits_ == 16)
		{
			oamBits_ = 0;
			oamIndex_++;
		}
	}
	else
	if (stop)
		// yield no OAM output
		writeWord(0);
}

void Dsp4::op0D()
{
	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
	}

	////////////////////////////////////////////////////
	// process initial inputs

	// sort inputs
	worldY_           = readDword();
	polyBottom_[0][0] = readWord();
	polyTop_[0][0]    = readWord();
	polyCx_[1][0]     = readWord();
	viewportBottom_   = readWord();
	worldX_           = readDword();
	polyCx_[0][0]     = readWord();
	polyPtr_[0][0]    = readWord();
	worldYofs_        = readWord();
	worldDy_          = readDword();
	worldDx_          = readDword();
	distance_          = readWord();
	readWord(); // 0x0000
	worldXenv_        = SEX78(readWord());
	worldDdy_         = readWord();
	worldDdx_         = readWord();
	viewYofsenv_      = readWord();

	// initial (x, y, offset) at starting raster line
	viewX1_    = (worldX_ + worldXenv_) >> 16;
	viewY1_    = worldY_ >> 16;
	viewXofs1_ = worldX_ >> 16;
	viewYofs1_ = worldYofs_;

	// first raster line
	polyRaster_[0][0] = polyBottom_[0][0];

	do
	{
		////////////////////////////////////////////////////
		// process one iteration of projection

		// perspective projection of world (x, y, scroll) points
		// based on the current projection lines
		viewX2_    = (((worldX_ + worldXenv_) >> 16) * distance_ >> 15) + (viewTurnoffX_ * distance_ >> 15);
		viewY2_    = (worldY_ >> 16) * distance_ >> 15;
		viewXofs2_ = viewX2_;
		viewYofs2_ = (worldYofs_ * distance_ >> 15) + polyBottom_[0][0] - viewY2_;

		// 1. World x-location before transformation
		// 2. Viewer x-position at the current
		// 3. World y-location before perspective projection
		// 4. Viewer y-position below the horizon
		// 5. Number of raster lines drawn in this iteration
		{ outCount_=0; outIndex_=0; }
		writeWord((worldX_ + worldXenv_) >> 16);
		writeWord(viewX2_);
		writeWord(worldY_ >> 16);
		writeWord(viewY2_);

		//////////////////////////////////////////////////////////

		// SR = 0x00

		// determine # of raster lines used
		segments_ = viewY1_ - viewY2_;

		// prevent overdraw
		if (viewY2_ >= polyRaster_[0][0])
			segments_ = 0;
		else
			polyRaster_[0][0] = viewY2_;

		// don't draw outside the window
		if (viewY2_ < polyTop_[0][0])
		{
			segments_ = 0;

			// flush remaining raster lines
			if (viewY1_ >= polyTop_[0][0])
				segments_ = viewY1_ - polyTop_[0][0];
		}

		// SR = 0x80

		writeWord(segments_);

		//////////////////////////////////////////////////////////

		// scan next command if no SR check needed
		if (segments_)
		{
			int32	px_dx, py_dy;
			int32	x_scroll, y_scroll;

			// SR = 0x00

			// linear interpolation (lerp) between projected points
			px_dx = (viewXofs2_ - viewXofs1_) * inv(segments_) << 1;
			py_dy = (viewYofs2_ - viewYofs1_) * inv(segments_) << 1;

			// starting step values
			x_scroll = SEX16(polyCx_[0][0] + viewXofs1_);
			y_scroll = SEX16(-viewportBottom_ + viewYofs1_ + viewYofsenv_ + polyCx_[1][0] - worldYofs_);

			// SR = 0x80

			// rasterize line
			for (lcv_ = 0; lcv_ < segments_; lcv_++)
			{
				// 1. HDMA memory pointer (bg1)
				// 2. vertical scroll offset ($210E)
				// 3. horizontal scroll offset ($210D)
				writeWord(polyPtr_[0][0]);
				writeWord((y_scroll + 0x8000) >> 16);
				writeWord((x_scroll + 0x8000) >> 16);

				// update memory address
				polyPtr_[0][0] -= 4;

				// update screen values
				x_scroll += px_dx;
				y_scroll += py_dy;
			}
		}

		/////////////////////////////////////////////////////
		// Post-update

		// update new viewer (x, y, scroll) to last raster line drawn
		viewX1_    = viewX2_;
		viewY1_    = viewY2_;
		viewXofs1_ = viewXofs2_;
		viewYofs1_ = viewYofs2_;

		// add deltas for projection lines
		worldDx_ += SEX78(worldDdx_);
		worldDy_ += SEX78(worldDdy_);

		// update projection lines
		worldX_ += (worldDx_ + worldXenv_);
		worldY_ += worldDy_;

		////////////////////////////////////////////////////
		// command check

		// scan next command
		inCount_ = 2;
		{ inIndex_=0; logic_=1; return; }

		resume1:

		// inspect input
		distance_ = readWord();

		// terminate op
		if (distance_ == -0x8000)
			break;

		// already have 2 bytes in queue
		inCount_ = 6;
		{ inIndex_=0; logic_=2; return; }

		resume2:

		// inspect inputs
		worldDdy_    = readWord();
		worldDdx_    = readWord();
		viewYofsenv_ = readWord();

		// no envelope here
		worldXenv_ = 0;
	}
	while (1);

	waiting4command_ = true;
}

void Dsp4::op0E()
{
	oamRowMax_ = 16;
	memset(oamRow_, 0, 64);
}

void Dsp4::op0F()
{
	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
		case 3: goto resume3; break;
		case 4: goto resume4; break;
	}

	////////////////////////////////////////////////////
	// process initial inputs

	// sort inputs
	readWord(); // 0x0000
	worldY_           = readDword();
	polyBottom_[0][0] = readWord();
	polyTop_[0][0]    = readWord();
	polyCx_[1][0]     = readWord();
	viewportBottom_   = readWord();
	worldX_           = readDword();
	polyCx_[0][0]     = readWord();
	polyPtr_[0][0]    = readWord();
	worldYofs_        = readWord();
	worldDy_          = readDword();
	worldDx_          = readDword();
	distance_          = readWord();
	readWord(); // 0x0000
	worldXenv_        = readDword();
	worldDdy_         = readWord();
	worldDdx_         = readWord();
	viewYofsenv_      = readWord();

	// initial (x, y, offset) at starting raster line
	viewX1_         = (worldX_ + worldXenv_) >> 16;
	viewY1_         = worldY_ >> 16;
	viewXofs1_      = worldX_ >> 16;
	viewYofs1_      = worldYofs_;
	viewTurnoffX_  = 0;
	viewTurnoffDx_ = 0;

	// first raster line
	polyRaster_[0][0] = polyBottom_[0][0];

	do
	{
		////////////////////////////////////////////////////
		// process one iteration of projection

		// perspective projection of world (x, y, scroll) points
		// based on the current projection lines
		viewX2_    = ((worldX_ + worldXenv_) >> 16) * distance_ >> 15;
		viewY2_    = (worldY_ >> 16) * distance_ >> 15;
		viewXofs2_ = viewX2_;
		viewYofs2_ = (worldYofs_ * distance_ >> 15) + polyBottom_[0][0] - viewY2_;

		// 1. World x-location before transformation
		// 2. Viewer x-position at the next
		// 3. World y-location before perspective projection
		// 4. Viewer y-position below the horizon
		// 5. Number of raster lines drawn in this iteration
		{ outCount_=0; outIndex_=0; }
		writeWord((worldX_ + worldXenv_) >> 16);
		writeWord(viewX2_);
		writeWord(worldY_ >> 16);
		writeWord(viewY2_);

		//////////////////////////////////////////////////////

		// SR = 0x00

		// determine # of raster lines used
		segments_ = polyRaster_[0][0] - viewY2_;

		// prevent overdraw
		if (viewY2_ >= polyRaster_[0][0])
			segments_ = 0;
		else
			polyRaster_[0][0] = viewY2_;

		// don't draw outside the window
		if (viewY2_ < polyTop_[0][0])
		{
			segments_ = 0;

			// flush remaining raster lines
			if (viewY1_ >= polyTop_[0][0])
				segments_ = viewY1_ - polyTop_[0][0];
		}

		// SR = 0x80

		writeWord(segments_);

		//////////////////////////////////////////////////////

		// scan next command if no SR check needed
		if (segments_)
		{
			int32	px_dx, py_dy;
			int32	x_scroll, y_scroll;

			for (lcv_ = 0; lcv_ < 4; lcv_++)
			{
				// grab inputs
				inCount_ = 4;
				{ inIndex_=0; logic_=1; return; }

				resume1:

				for (;;)
				{
					int16	dist;
					int16	color, red, green, blue;

					dist  = readWord();
					color = readWord();

					// U1+B5+G5+R5
					red   =  color        & 0x1f;
					green = (color >>  5) & 0x1f;
					blue  = (color >> 10) & 0x1f;

					// dynamic lighting
					red   = (red   * dist >> 15) & 0x1f;
					green = (green * dist >> 15) & 0x1f;
					blue  = (blue  * dist >> 15) & 0x1f;
					color = red | (green << 5) | (blue << 10);

					{ outCount_=0; outIndex_=0; }
					writeWord(color);

					break;
				}
			}

			//////////////////////////////////////////////////////

			// SR = 0x00

			// linear interpolation (lerp) between projected points
			px_dx = (viewXofs2_ - viewXofs1_) * inv(segments_) << 1;
			py_dy = (viewYofs2_ - viewYofs1_) * inv(segments_) << 1;

			// starting step values
			x_scroll = SEX16(polyCx_[0][0] + viewXofs1_);
			y_scroll = SEX16(-viewportBottom_ + viewYofs1_ + viewYofsenv_ + polyCx_[1][0] - worldYofs_);

			// SR = 0x80

			// rasterize line
			for (lcv_ = 0; lcv_ < segments_; lcv_++)
			{
				// 1. HDMA memory pointer
				// 2. vertical scroll offset ($210E)
				// 3. horizontal scroll offset ($210D)
				writeWord(polyPtr_[0][0]);
				writeWord((y_scroll + 0x8000) >> 16);
				writeWord((x_scroll + 0x8000) >> 16);

				// update memory address
				polyPtr_[0][0] -= 4;

				// update screen values
				x_scroll += px_dx;
				y_scroll += py_dy;
			}
		}

		////////////////////////////////////////////////////
		// Post-update

		// update new viewer (x, y, scroll) to last raster line drawn
		viewX1_    = viewX2_;
		viewY1_    = viewY2_;
		viewXofs1_ = viewXofs2_;
		viewYofs1_ = viewYofs2_;

		// add deltas for projection lines
		worldDx_ += SEX78(worldDdx_);
		worldDy_ += SEX78(worldDdy_);

		// update projection lines
		worldX_ += (worldDx_ + worldXenv_);
		worldY_ += worldDy_;

		// update road turnoff position
		viewTurnoffX_ += viewTurnoffDx_;

		////////////////////////////////////////////////////
		// command check

		// scan next command
		inCount_ = 2;
		{ inIndex_=0; logic_=2; return; }

		resume2:

		// check for termination
		distance_ = readWord();
		if (distance_ == -0x8000)
			break;

		// road splice
		if ((uint16) distance_ == 0x8001)
		{
			inCount_ = 6;
			{ inIndex_=0; logic_=3; return; }

			resume3:

			distance_        = readWord();
			viewTurnoffX_  = readWord();
			viewTurnoffDx_ = readWord();

			// factor in new changes
			viewX1_    += (viewTurnoffX_ * distance_ >> 15);
			viewXofs1_ += (viewTurnoffX_ * distance_ >> 15);

			// update stepping values
			viewTurnoffX_ += viewTurnoffDx_;

			inCount_ = 2;
			{ inIndex_=0; logic_=2; return; }
		}

		// already have 2 bytes in queue
		inCount_ = 6;
		{ inIndex_=0; logic_=4; return; }

		resume4:

		// inspect inputs
		worldDdy_    = readWord();
		worldDdx_    = readWord();
		viewYofsenv_ = readWord();

		// no envelope here
		worldXenv_ = 0;
	}
	while (1);

	// terminate op
	waiting4command_ = true;
}

void Dsp4::op10()
{
	waiting4command_ = false;

	// op flow control
	switch (logic_)
	{
		case 1: goto resume1; break;
		case 2: goto resume2; break;
		case 3: goto resume3; break;
	}

	////////////////////////////////////////////////////
	// sort inputs

	readWord(); // 0x0000
	worldY_           = readDword();
	polyBottom_[0][0] = readWord();
	polyTop_[0][0]    = readWord();
	polyCx_[1][0]     = readWord();
	viewportBottom_   = readWord();
	worldX_           = readDword();
	polyCx_[0][0]     = readWord();
	polyPtr_[0][0]    = readWord();
	worldYofs_        = readWord();
	distance_          = readWord();
	viewY2_           = readWord();
	viewDy_           = readWord() * distance_ >> 15;
	viewX2_           = readWord();
	viewDx_           = readWord() * distance_ >> 15;
	viewYofsenv_      = readWord();

	// initial (x, y, offset) at starting raster line
	viewX1_    = worldX_ >> 16;
	viewY1_    = worldY_ >> 16;
	viewXofs1_ = viewX1_;
	viewYofs1_ = worldYofs_;

	// first raster line
	polyRaster_[0][0] = polyBottom_[0][0];

	do
	{
		////////////////////////////////////////////////////
		// process one iteration of projection

		// add shaping
		viewX2_ += viewDx_;
		viewY2_ += viewDy_;

		// vertical scroll calculation
		viewXofs2_ = viewX2_;
		viewYofs2_ = (worldYofs_ * distance_ >> 15) + polyBottom_[0][0] - viewY2_;

		// 1. Viewer x-position at the next
		// 2. Viewer y-position below the horizon
		// 3. Number of raster lines drawn in this iteration
		{ outCount_=0; outIndex_=0; }
		writeWord(viewX2_);
		writeWord(viewY2_);

		//////////////////////////////////////////////////////

		// SR = 0x00

		// determine # of raster lines used
		segments_ = viewY1_ - viewY2_;

		// prevent overdraw
		if (viewY2_ >= polyRaster_[0][0])
			segments_ = 0;
		else
			polyRaster_[0][0] = viewY2_;

		// don't draw outside the window
		if (viewY2_ < polyTop_[0][0])
		{
			segments_ = 0;

			// flush remaining raster lines
			if (viewY1_ >= polyTop_[0][0])
				segments_ = viewY1_ - polyTop_[0][0];
		}

		// SR = 0x80

		writeWord(segments_);

		//////////////////////////////////////////////////////

		// scan next command if no SR check needed
		if (segments_)
		{
			for (lcv_ = 0; lcv_ < 4; lcv_++)
			{
				// grab inputs
				inCount_ = 4;
				{ inIndex_=0; logic_=1; return; }

				resume1:

				for (;;)
				{
					int16	dist;
					int16	color, red, green, blue;

					dist  = readWord();
					color = readWord();

					// U1+B5+G5+R5
					red   =  color        & 0x1f;
					green = (color >>  5) & 0x1f;
					blue  = (color >> 10) & 0x1f;

					// dynamic lighting
					red   = (red   * dist >> 15) & 0x1f;
					green = (green * dist >> 15) & 0x1f;
					blue  = (blue  * dist >> 15) & 0x1f;
					color = red | (green << 5) | (blue << 10);

					{ outCount_=0; outIndex_=0; }
					writeWord(color);

					break;
				}
			}
		}

		//////////////////////////////////////////////////////

		// scan next command if no SR check needed
		if (segments_)
		{
			int32	px_dx, py_dy;
			int32	x_scroll, y_scroll;

			// SR = 0x00

			// linear interpolation (lerp) between projected points
			px_dx = (viewXofs2_ - viewXofs1_) * inv(segments_) << 1;
			py_dy = (viewYofs2_ - viewYofs1_) * inv(segments_) << 1;

			// starting step values
			x_scroll = SEX16(polyCx_[0][0] + viewXofs1_);
			y_scroll = SEX16(-viewportBottom_ + viewYofs1_ + viewYofsenv_ + polyCx_[1][0] - worldYofs_);

			// SR = 0x80

			// rasterize line
			for (lcv_ = 0; lcv_ < segments_; lcv_++)
			{
				// 1. HDMA memory pointer (bg2)
				// 2. vertical scroll offset ($2110)
				// 3. horizontal scroll offset ($210F)
				writeWord(polyPtr_[0][0]);
				writeWord((y_scroll + 0x8000) >> 16);
				writeWord((x_scroll + 0x8000) >> 16);

				// update memory address
				polyPtr_[0][0] -= 4;

				// update screen values
				x_scroll += px_dx;
				y_scroll += py_dy;
			}
		}

		/////////////////////////////////////////////////////
		// Post-update

		// update new viewer (x, y, scroll) to last raster line drawn
		viewX1_    = viewX2_;
		viewY1_    = viewY2_;
		viewXofs1_ = viewXofs2_;
		viewYofs1_ = viewYofs2_;

		////////////////////////////////////////////////////
		// command check

		// scan next command
		inCount_ = 2;
		{ inIndex_=0; logic_=2; return; }

		resume2:

		// check for opcode termination
		distance_ = readWord();
		if (distance_ == -0x8000)
			break;

		// already have 2 bytes in queue
		inCount_ = 10;
		{ inIndex_=0; logic_=3; return; }

		resume3:

		// inspect inputs
		viewY2_ = readWord();
		viewDy_ = readWord() * distance_ >> 15;
		viewX2_ = readWord();
		viewDx_ = readWord() * distance_ >> 15;
	}
	while (1);

	waiting4command_ = true;
}

void Dsp4::op11(int16 A, int16 B, int16 C, int16 D, int16 *M)
{
	// 0x155 = 341 = Horizontal Width of the Screen
	*M = ((A * 0x0155 >> 2) & 0xf000) | ((B * 0x0155 >> 6) & 0x0f00) | ((C * 0x0155 >> 10) & 0x00f0) | ((D * 0x0155 >> 14) & 0x000f);
}

void Dsp4::setByte(){
	// clear pending read
	if (outIndex_ < outCount_)
	{
		outIndex_++;
		return;
	}

	if (waiting4command_)
	{
		if (halfCommand_)
		{
			command_ |= (byte_ << 8);
			inIndex_        = 0;
			waiting4command_ = false;
			halfCommand_    = false;
			outCount_       = 0;
			outIndex_       = 0;

			logic_ = 0;

			switch (command_)
			{
				case 0x0000: inCount_ =  4; break;
				case 0x0001: inCount_ = 44; break;
				case 0x0003: inCount_ =  0; break;
				case 0x0005: inCount_ =  0; break;
				case 0x0006: inCount_ =  0; break;
				case 0x0007: inCount_ = 34; break;
				case 0x0008: inCount_ = 90; break;
				case 0x0009: inCount_ = 14; break;
				case 0x000a: inCount_ =  6; break;
				case 0x000b: inCount_ =  6; break;
				case 0x000d: inCount_ = 42; break;
				case 0x000e: inCount_ =  0; break;
				case 0x000f: inCount_ = 46; break;
				case 0x0010: inCount_ = 36; break;
				case 0x0011: inCount_ =  8; break;
				default:
					waiting4command_ = true;
					break;
			}
		}
		else
		{
			command_ = byte_;
			halfCommand_ = true;
		}
	}
	else
	{
		params_[inIndex_] = byte_;
		inIndex_++;
	}

	if (!waiting4command_ && inCount_ == inIndex_)
	{
		// Actually execute the command
		waiting4command_ = true;
		outIndex_       = 0;
		inIndex_        = 0;

		switch (command_)
		{
			// 16-bit multiplication
			case 0x0000:
			{
				int16	multiplier, multiplicand;
				int32	product;

				multiplier   = readWord();
				multiplicand = readWord();

				multiply(multiplicand, multiplier, &product);

				{ outCount_=0; outIndex_=0; }
				writeWord(product);
				writeWord(product >> 16);

				break;
			}

			// single-player track projection
			case 0x0001:
				op01();
				break;

			// single-player selection
			case 0x0003:
				op03();
				break;

			// clear OAM
			case 0x0005:
				op05();
				break;

			// transfer OAM
			case 0x0006:
				op06();
				break;

			// single-player track turnoff projection
			case 0x0007:
				op07();
				break;

			// solid polygon projection
			case 0x0008:
				op08();
				break;

			// sprite projection
			case 0x0009:
				op09();
				break;

			// unknown
			case 0x000A:
			{
				readWord();
				int16	in2a = readWord();
				readWord();
				int16	out1a, out2a, out3a, out4a;

				op0A(in2a, &out2a, &out1a, &out4a, &out3a);

				{ outCount_=0; outIndex_=0; }
				writeWord(out1a);
				writeWord(out2a);
				writeWord(out3a);
				writeWord(out4a);

				break;
			}

			// set OAM
			case 0x000B:
			{
				int16	sp_x    = readWord();
				int16	sp_y    = readWord();
				int16	sp_attr = readWord();
				bool	draw = true;

				{ outCount_=0; outIndex_=0; }
				op0B(&draw, sp_x, sp_y, sp_attr, 0, 1);

				break;
			}

			// multi-player track projection
			case 0x000D:
				op0D();
				break;

			// multi-player selection
			case 0x000E:
				op0E();
				break;

			// single-player track projection with lighting
			case 0x000F:
				op0F();
				break;

			// single-player track turnoff projection with lighting
			case 0x0010:
				op10();
				break;

			// unknown: horizontal mapping command
			case 0x0011:
			{
				int16	a, b, c, d, m;

				d = readWord();
				c = readWord();
				b = readWord();
				a = readWord();

				op11(a, b, c, d, &m);

				{ outCount_=0; outIndex_=0; }
				writeWord(m);

				break;
			}

			default:
				break;
		}
	}
}

void Dsp4::getByte(){
	if (outCount_)
	{
		byte_ = (uint8) output_[outIndex_ & 0x1FF];

		outIndex_++;
		if (outCount_ == outIndex_)
			outCount_ = 0;
	}
	else
		byte_ = 0xff;
}


Dsp4::Dsp4(){ power(); }
auto Dsp4::handles(uint24 address) const -> bool { uint32 b=address>>16; uint32 o=address & 0xFFFF; bool bb=(b>=0x30&&b<=0x3F)||(b>=0xB0&&b<=0xBF); return bb && o>=0x8000; }
void Dsp4::power(){ waiting4command_=true; halfCommand_=false; command_=0; inCount_=inIndex_=outCount_=outIndex_=0; byte_=0; address_=0; logic_=0; std::memset(params_,0,sizeof(params_)); std::memset(output_,0,sizeof(output_)); lcv_=distance_=raster_=segments_=0; worldX_=worldY_=worldDx_=worldDy_=0; worldDdx_=worldDdy_=0; worldXenv_=0; worldYofs_=viewX1_=viewY1_=viewX2_=viewY2_=viewDx_=viewDy_=viewXofs1_=viewYofs1_=viewXofs2_=viewYofs2_=viewYofsenv_=viewTurnoffX_=viewTurnoffDx_=0; viewportCx_=viewportCy_=viewportLeft_=viewportRight_=viewportTop_=viewportBottom_=0; std::memset(polyClipLf_,0,sizeof(polyClipLf_)); std::memset(polyClipRt_,0,sizeof(polyClipRt_)); std::memset(polyPtr_,0,sizeof(polyPtr_)); std::memset(polyRaster_,0,sizeof(polyRaster_)); std::memset(polyTop_,0,sizeof(polyTop_)); std::memset(polyBottom_,0,sizeof(polyBottom_)); std::memset(polyCx_,0,sizeof(polyCx_)); polyStart_[0]=polyStart_[1]=polyPlane_[0]=polyPlane_[1]=0; spriteX_=spriteY_=spriteAttr_=0; spriteSize_=false; spriteClipy_=spriteCount_=0; std::memset(oamAttr_,0,sizeof(oamAttr_)); oamIndex_=oamBits_=oamRowMax_=0; std::memset(oamRow_,0,sizeof(oamRow_)); }
void Dsp4::writeWord(int16 v){ output_[outCount_]=uint8(v); output_[outCount_+1]=uint8(v>>8); outCount_+=2; }
void Dsp4::writeByte(uint8 v){ output_[outCount_++]=v; }
void Dsp4::write(uint24 address, uint8 data){ uint16 a=uint16(address & 0xFFFF); if(a>=0xC000) return; byte_=data; address_=a; setByte(); }
uint8 Dsp4::read(uint24 address){ uint16 a=uint16(address & 0xFFFF); if(a>=0xC000) return 0x80; address_=a; getByte(); return byte_; }
void Dsp4::serialize(Writer& w) const { w.b(waiting4command_); w.b(halfCommand_); w.u16(command_); w.u32(inCount_); w.u32(inIndex_); w.u32(outCount_); w.u32(outIndex_); w.raw(params_,sizeof(params_)); w.raw(output_,sizeof(output_)); w.u8(byte_); w.u16(address_); w.u8((uint8)logic_); w.u16((uint16)lcv_); w.u16((uint16)distance_); w.u16((uint16)raster_); w.u16((uint16)segments_); w.u32((uint32)worldX_); w.u32((uint32)worldY_); w.u32((uint32)worldDx_); w.u32((uint32)worldDy_); w.u16((uint16)worldDdx_); w.u16((uint16)worldDdy_); w.u32((uint32)worldXenv_); w.u16((uint16)worldYofs_); w.u16((uint16)viewX1_); w.u16((uint16)viewY1_); w.u16((uint16)viewX2_); w.u16((uint16)viewY2_); w.u16((uint16)viewDx_); w.u16((uint16)viewDy_); w.u16((uint16)viewXofs1_); w.u16((uint16)viewYofs1_); w.u16((uint16)viewXofs2_); w.u16((uint16)viewYofs2_); w.u16((uint16)viewYofsenv_); w.u16((uint16)viewTurnoffX_); w.u16((uint16)viewTurnoffDx_); w.u16((uint16)viewportCx_); w.u16((uint16)viewportCy_); w.u16((uint16)viewportLeft_); w.u16((uint16)viewportRight_); w.u16((uint16)viewportTop_); w.u16((uint16)viewportBottom_); w.raw(polyClipLf_,sizeof(polyClipLf_)); w.raw(polyClipRt_,sizeof(polyClipRt_)); w.raw(polyPtr_,sizeof(polyPtr_)); w.raw(polyRaster_,sizeof(polyRaster_)); w.raw(polyTop_,sizeof(polyTop_)); w.raw(polyBottom_,sizeof(polyBottom_)); w.raw(polyCx_,sizeof(polyCx_)); w.raw(polyStart_,sizeof(polyStart_)); w.raw(polyPlane_,sizeof(polyPlane_)); w.u16((uint16)spriteX_); w.u16((uint16)spriteY_); w.u16((uint16)spriteAttr_); w.b(spriteSize_); w.u16((uint16)spriteClipy_); w.u16((uint16)spriteCount_); w.raw(oamAttr_,sizeof(oamAttr_)); w.u16((uint16)oamIndex_); w.u16((uint16)oamBits_); w.u16((uint16)oamRowMax_); w.raw(oamRow_,sizeof(oamRow_)); }
void Dsp4::deserialize(Reader& r){ waiting4command_=r.b(); halfCommand_=r.b(); command_=r.u16(); inCount_=r.u32(); inIndex_=r.u32(); outCount_=r.u32(); outIndex_=r.u32(); r.raw(params_,sizeof(params_)); r.raw(output_,sizeof(output_)); byte_=r.u8(); address_=r.u16(); logic_=int8(r.u8()); lcv_=int16(r.u16()); distance_=int16(r.u16()); raster_=int16(r.u16()); segments_=int16(r.u16()); worldX_=int32(r.u32()); worldY_=int32(r.u32()); worldDx_=int32(r.u32()); worldDy_=int32(r.u32()); worldDdx_=int16(r.u16()); worldDdy_=int16(r.u16()); worldXenv_=int32(r.u32()); worldYofs_=int16(r.u16()); viewX1_=int16(r.u16()); viewY1_=int16(r.u16()); viewX2_=int16(r.u16()); viewY2_=int16(r.u16()); viewDx_=int16(r.u16()); viewDy_=int16(r.u16()); viewXofs1_=int16(r.u16()); viewYofs1_=int16(r.u16()); viewXofs2_=int16(r.u16()); viewYofs2_=int16(r.u16()); viewYofsenv_=int16(r.u16()); viewTurnoffX_=int16(r.u16()); viewTurnoffDx_=int16(r.u16()); viewportCx_=int16(r.u16()); viewportCy_=int16(r.u16()); viewportLeft_=int16(r.u16()); viewportRight_=int16(r.u16()); viewportTop_=int16(r.u16()); viewportBottom_=int16(r.u16()); r.raw(polyClipLf_,sizeof(polyClipLf_)); r.raw(polyClipRt_,sizeof(polyClipRt_)); r.raw(polyPtr_,sizeof(polyPtr_)); r.raw(polyRaster_,sizeof(polyRaster_)); r.raw(polyTop_,sizeof(polyTop_)); r.raw(polyBottom_,sizeof(polyBottom_)); r.raw(polyCx_,sizeof(polyCx_)); r.raw(polyStart_,sizeof(polyStart_)); r.raw(polyPlane_,sizeof(polyPlane_)); spriteX_=int16(r.u16()); spriteY_=int16(r.u16()); spriteAttr_=int16(r.u16()); spriteSize_=r.b(); spriteClipy_=int16(r.u16()); spriteCount_=int16(r.u16()); r.raw(oamAttr_,sizeof(oamAttr_)); oamIndex_=int16(r.u16()); oamBits_=int16(r.u16()); oamRowMax_=int16(r.u16()); r.raw(oamRow_,sizeof(oamRow_)); }
} // namespace snes