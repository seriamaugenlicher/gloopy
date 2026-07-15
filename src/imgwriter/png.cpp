/*
A minimal PNG writer: enough of DEFLATE to produce a properly compressed,
standards-conformant file, and no more.

Written out rather than pulled in because the core deliberately has no
dependencies - no SDL, no libpng, no zlib - and a seal printer is not a reason to
acquire three. What is here is the classic construction: fixed Huffman codes (so
no code tables need to be built or emitted) over LZ77 matches found with a hash
chain. Pixel art with flat colour is close to the best case for it.
*/

#include "png.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace ImageWriter
{
namespace
{

/* ---- checksums --------------------------------------------------------- */

uint32_t crc32_of(const uint8_t* data, size_t len, uint32_t crc = 0)
{
	static uint32_t table[256];
	static bool built = false;
	if (!built)
	{
		for (uint32_t n = 0; n < 256; n++)
		{
			uint32_t c = n;
			for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		built = true;
	}

	crc = ~crc;
	for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

uint32_t adler32_of(const uint8_t* data, size_t len)
{
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < len; i++)
	{
		a = (a + data[i]) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}

/* ---- DEFLATE, fixed Huffman -------------------------------------------- */

//Bits go out least-significant first, but a Huffman code is defined
//most-significant first, so the two need different entry points
struct BitWriter
{
	std::vector<uint8_t>& out;
	uint32_t buffer = 0;
	int count = 0;

	explicit BitWriter(std::vector<uint8_t>& sink) : out(sink) {}

	void bits(uint32_t value, int n)
	{
		buffer |= (value & ((1u << n) - 1)) << count;
		count += n;
		while (count >= 8)
		{
			out.push_back((uint8_t)(buffer & 0xFF));
			buffer >>= 8;
			count -= 8;
		}
	}

	void code(uint32_t huffman, int n)
	{
		for (int i = n - 1; i >= 0; i--) bits((huffman >> i) & 1, 1);
	}

	void flush()
	{
		if (count > 0)
		{
			out.push_back((uint8_t)(buffer & 0xFF));
			buffer = 0;
			count = 0;
		}
	}
};

//RFC 1951, section 3.2.6
void literal(BitWriter& bw, int symbol)
{
	if (symbol <= 143)
		bw.code(0x30 + symbol, 8);
	else if (symbol <= 255)
		bw.code(0x190 + (symbol - 144), 9);
	else if (symbol <= 279)
		bw.code(symbol - 256, 7);
	else
		bw.code(0xC0 + (symbol - 280), 8);
}

const uint16_t LENGTH_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19,  23, 27,
								  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
								  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DIST_BASE[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
								33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
								1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3,  4,  4,  5,  5,  6,
								6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void match(BitWriter& bw, int length, int distance)
{
	int l = 28;
	while (l > 0 && LENGTH_BASE[l] > length) l--;
	literal(bw, 257 + l);
	bw.bits(length - LENGTH_BASE[l], LENGTH_EXTRA[l]);

	int d = 29;
	while (d > 0 && DIST_BASE[d] > distance) d--;
	bw.code(d, 5);
	bw.bits(distance - DIST_BASE[d], DIST_EXTRA[d]);
}

constexpr int WINDOW = 32768;
constexpr int MIN_MATCH = 3;
constexpr int MAX_MATCH = 258;
constexpr int HASH_BITS = 15;
constexpr int HASH_SIZE = 1 << HASH_BITS;
constexpr int MAX_CHAIN = 128;  //cap the search; the last few bytes are never worth the time

inline uint32_t hash3(const uint8_t* p)
{
	return ((p[0] << 10) ^ (p[1] << 5) ^ p[2]) & (HASH_SIZE - 1);
}

std::vector<uint8_t> deflate(const std::vector<uint8_t>& in)
{
	std::vector<uint8_t> out;
	BitWriter bw(out);
	bw.bits(1, 1);  //final block
	bw.bits(1, 2);  //fixed Huffman

	std::vector<int> head((size_t)HASH_SIZE, -1);
	std::vector<int> prev(in.size(), -1);

	size_t pos = 0;
	while (pos < in.size())
	{
		int best_len = 0;
		int best_dist = 0;

		if (pos + MIN_MATCH <= in.size())
		{
			uint32_t h = hash3(&in[pos]);
			int candidate = head[h];
			int chain = MAX_CHAIN;

			while (candidate >= 0 && chain-- > 0)
			{
				size_t dist = pos - (size_t)candidate;
				if (dist > WINDOW) break;

				size_t max_len = std::min((size_t)MAX_MATCH, in.size() - pos);
				size_t len = 0;
				while (len < max_len && in[(size_t)candidate + len] == in[pos + len]) len++;

				if ((int)len > best_len)
				{
					best_len = (int)len;
					best_dist = (int)dist;
					if (best_len >= MAX_MATCH) break;
				}
				candidate = prev[(size_t)candidate];
			}

			prev[pos] = head[h];
			head[h] = (int)pos;
		}

		if (best_len >= MIN_MATCH)
		{
			match(bw, best_len, best_dist);

			//every position inside the match still has to enter the hash chain, or
			//later matches cannot see back into it
			for (int i = 1; i < best_len; i++)
			{
				size_t p = pos + (size_t)i;
				if (p + MIN_MATCH <= in.size())
				{
					uint32_t h = hash3(&in[p]);
					prev[p] = head[h];
					head[h] = (int)p;
				}
			}
			pos += (size_t)best_len;
		}
		else
		{
			literal(bw, in[pos]);
			pos++;
		}
	}

	literal(bw, 256);  //end of block
	bw.flush();
	return out;
}

/* ---- PNG container ------------------------------------------------------ */

void be32(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back((uint8_t)(v >> 24));
	out.push_back((uint8_t)(v >> 16));
	out.push_back((uint8_t)(v >> 8));
	out.push_back((uint8_t)v);
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
	be32(out, (uint32_t)data.size());

	std::vector<uint8_t> crc_input;
	crc_input.insert(crc_input.end(), type, type + 4);
	crc_input.insert(crc_input.end(), data.begin(), data.end());

	out.insert(out.end(), crc_input.begin(), crc_input.end());
	be32(out, crc32_of(crc_input.data(), crc_input.size()));
}

}  //namespace

bool save_png_24(const fs::path& path, uint32_t width, uint32_t height, const uint32_t* argb)
{
	if (!width || !height)
	{
		return false;
	}

	//Scanlines, each prefixed with the filter used to encode it. Filtering is what
	//makes PNG worth using: without it an image whose pixels are all different -
	//a gradient, anything dithered - gives LZ77 nothing to match and comes out
	//larger than the equivalent BMP. Predicting each byte from its neighbours turns
	//that into a run of small numbers, which does compress.
	const size_t stride = (size_t)width * 3;

	std::vector<uint8_t> line(stride);
	std::vector<uint8_t> prior(stride, 0);
	std::vector<uint8_t> candidate(stride);
	std::vector<uint8_t> best(stride);

	std::vector<uint8_t> raw;
	raw.reserve((stride + 1) * height);

	for (uint32_t y = 0; y < height; y++)
	{
		const uint32_t* row = argb + (size_t)y * width;
		for (uint32_t x = 0; x < width; x++)
		{
			line[x * 3 + 0] = (uint8_t)(row[x] >> 16);  //R
			line[x * 3 + 1] = (uint8_t)(row[x] >> 8);   //G
			line[x * 3 + 2] = (uint8_t)(row[x]);        //B
		}

		//Try each filter and keep whichever leaves the smallest values, treating a
		//byte as signed. This is the heuristic the PNG spec itself suggests, and it
		//stands in for actually compressing the row five times over.
		int best_filter = 0;
		size_t best_score = SIZE_MAX;

		for (int filter = 0; filter <= 4; filter++)
		{
			size_t score = 0;
			for (size_t i = 0; i < stride; i++)
			{
				int raw_byte = line[i];
				int left = i >= 3 ? line[i - 3] : 0;
				int up = prior[i];
				int upleft = i >= 3 ? prior[i - 3] : 0;

				int value;
				switch (filter)
				{
				case 1: value = raw_byte - left; break;
				case 2: value = raw_byte - up; break;
				case 3: value = raw_byte - ((left + up) >> 1); break;
				case 4:
				{
					int p = left + up - upleft;
					int pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - upleft);
					int predictor = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : upleft);
					value = raw_byte - predictor;
					break;
				}
				default: value = raw_byte; break;
				}

				candidate[i] = (uint8_t)value;
				score += (size_t)std::abs((int8_t)candidate[i]);
			}

			if (score < best_score)
			{
				best_score = score;
				best_filter = filter;
				best = candidate;
			}
		}

		raw.push_back((uint8_t)best_filter);
		raw.insert(raw.end(), best.begin(), best.end());
		prior = line;
	}

	std::vector<uint8_t> idat;
	idat.push_back(0x78);  //zlib: deflate, 32K window
	idat.push_back(0x01);  //  check bits, no preset dictionary
	std::vector<uint8_t> compressed = deflate(raw);
	idat.insert(idat.end(), compressed.begin(), compressed.end());
	be32(idat, adler32_of(raw.data(), raw.size()));

	std::vector<uint8_t> ihdr;
	be32(ihdr, width);
	be32(ihdr, height);
	ihdr.push_back(8);  //bit depth
	ihdr.push_back(2);  //colour type 2: truecolour RGB
	ihdr.push_back(0);  //deflate
	ihdr.push_back(0);  //adaptive filtering
	ihdr.push_back(0);  //no interlace

	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	chunk(png, "IHDR", ihdr);
	chunk(png, "IDAT", idat);
	chunk(png, "IEND", {});

	std::ofstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		return false;
	}
	file.write((const char*)png.data(), (std::streamsize)png.size());
	return file.good();
}

}  //namespace ImageWriter
