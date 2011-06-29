/*
 * Copyright (C) 2011 Kelly Anderson <cbxbiker61@gmail.com>
 * Copyright (C) 2010 Federico Heinz <fheinz@vialibre.org.ar>
 *
 * Read COPYING file distributed with this file for LICENSING information.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <algorithm>
#include "ecc_rs.h"

using namespace std;

namespace {
const char programVersion[] = "plugenv version 1.0";

void usage(const string &progname)
{
	cout << "Usage: " << progname << " -e|-h|-l|-v|-w envFile" << endl;
	cout << " -e: edit and write env" << endl;
	cout << " -h: help" << endl;
	cout << " -l: list env" << endl;
	cout << " -v: version" << endl;
	cout << " -w: write envFile to nand" << endl;
	exit(0);
}

#define NAND_CHUNK_SIZE 2048
#define NAND_CHUNK_COUNT 64
#define ENV_SIZE (NAND_CHUNK_COUNT*NAND_CHUNK_SIZE) // 128K
#define ECC_CHUNK_SIZE  512
#define ECC_SIZE 10

union Oob
{
	uint8_t b[64];
	struct oob_data_s
	{
		unsigned char filler[24];
		unsigned char ecc_buffers[4][ECC_SIZE];
	} data;
};

union Crc
{
	uint32_t i;
	uint8_t b[4];
};

typedef basic_string<uint8_t> u8string;

string validateSystem(const string &progname);
void list(const string &mtdDev);
void edit(const string &mtdDev);
void write(const string &mtdDev, const string &envFile);
string getEnvString(const string &mtdDev);
u8string getOutput(string cmd);
string getOutputString(string cmd);
u8string encodeEnv(ifstream *);
string decodeEnvText(u8string env);
u8string decodeEnv(u8string env);
u8string encodeNandRs(u8string env);
u8string decodeNandRs(u8string nandRs);
uint32_t crc32(const u8string &);
uint32_t crc32(uint32_t crc, const uint8_t *buf, unsigned int len);

}; // anonymous namespace

int main(int argc, char *argv[])
{
	string progname(argv[0]);
	int optCount(0);
	bool ed(false);
	bool ls(false);
	bool wr(false);
	string envFile;

	int c;
	while ((c = getopt(argc, argv, "ehlvw:")) != -1)
	{
		switch(c)
		{
			case 'e':
				ed = true;
				++optCount;
				break;
			case 'h':
				usage(progname);
				break;
			case 'l':
				ls = true;
				++optCount;
				break;
			case 'v':
				cout << programVersion << endl;
				exit(1);
				break;
			case 'w':
				wr = true;
				envFile = optarg;
				++optCount;
				break;
			default:
				usage(progname);
				break;
		}
	}

	if ( optCount != 1 )
		usage(progname);

	string mtdDev(validateSystem(progname));

	if ( wr )
		write(mtdDev, envFile);
	else if ( ed )
		edit(mtdDev);
	else if ( ls )
		list(mtdDev);

	return 0;
}

namespace {

string validateSystem(const string &progname)
{
	bool foundSheeva = false;
	string mtdDev;

	{
		ifstream ifs("/proc/cpuinfo");

		while ( ifs )
		{
			char buf[512];
			ifs.getline(buf, sizeof(buf));
			string s(buf);

			if ( s.find("SheevaPlug") != string::npos )
			{
				foundSheeva = true;
				break;
			}
		}
	}

	if ( ! foundSheeva )
	{
		cerr << progname << " is intended for use on the SheevaPlug" << endl;
		exit(1);
	}

	bool foundUBoot = false;

	{
		ifstream ifs("/proc/mtd");

		while ( ifs )
		{
			char buf[128];
			ifs.getline(buf, sizeof(buf));
			string s(buf);

			if ( s.find("\"u-boot\"") != string::npos )
			{
				mtdDev = "/dev/";
				mtdDev += s.substr(0, 4);
				foundUBoot = true;
				break;
			}
		}
	}

	if ( ! foundUBoot )
	{
		cerr << progname << ": unable to find u-boot in /proc/mtd" << endl;
		cerr << "\t\tyou will have to specify mtdparts in your uboot env" << endl;
		cerr << "\t\tfrom within u-boot before this will work" << endl;
		exit(1);
	}

	if ( geteuid() != 0 )
	{
		cerr << progname << ": you must be root to run this program" << endl;
		exit(1);
	}

	return mtdDev;
}

void list(const string &mtdDev)
{
	cout << getEnvString(mtdDev);
}

void edit(const string &mtdDev)
{
	string envStr(getEnvString(mtdDev));

	string tmpFilEnv("/tmp/UBoot-Env.env");

	{
		ofstream viTemp(tmpFilEnv.c_str());
		viTemp << envStr;
	}

	struct stat stEnv1;
	stat(tmpFilEnv.c_str(), &stEnv1);

	string editCommand("vi");
	if ( char *ed = getenv("EDITOR") )
		editCommand = ed;

	editCommand += " ";
	editCommand += tmpFilEnv;

	system(editCommand.c_str());

	struct stat stEnv2;
	stat(tmpFilEnv.c_str(), &stEnv2);

	if ( stEnv2.st_mtime > stEnv1.st_mtime )
		write(mtdDev, tmpFilEnv);
}

void write(const string &mtdDev, const string &envFile)
{
	string tmpFilNand("/tmp/UBoot-Env.nand");
	u8string env;

	{
		ifstream in(envFile.c_str());
		env = encodeEnv(&in);
	}

	u8string nandRs(encodeNandRs(env));

	if ( env != decodeNandRs(nandRs) )
	{
		cerr << "encodeEnv->encodeNandRs->decodeNandRs fails" << endl;
		exit(1);
	}

	{
		ofstream out(tmpFilNand.c_str());
		out.write((const char*)nandRs.data(), nandRs.length());
	}

	string eraseCmd = "flash_erase " + mtdDev + " 0xa0000 1";
	string writeCmd = "nandwrite -f -n -o -s 0xa0000 "
			+ mtdDev + " " + tmpFilNand;

	cout << eraseCmd << endl;
	cout << getOutputString(eraseCmd) << endl;
	cout << writeCmd << endl;
	cout << getOutputString(writeCmd) << endl;
}

string getEnvString(const string &mtdDev)
{
	string verS(getOutputString("nanddump --version").substr(9, 4));
	float ver(atof(verS.c_str()));
	string cmd;

	if ( ver > 1.30 )
		cmd = "nanddump -q --bb=padbad --oob -n -s 0xa0000 -l 0x20000 " + mtdDev;
	else
		cmd = "nanddump -q -n -s 0xa0000 -l 0x20000 " + mtdDev;

	u8string s(getOutput(cmd));
	return decodeEnvText(decodeNandRs(s));
}

u8string getOutput(string cmd)
{
	u8string data;
	uint8_t buf[2048];

	FILE *stream = popen(cmd.c_str(), "r");

	if ( ! stream )
	{
		cerr << "getOutput(): failed to run " << cmd << endl;
		exit(1);
	}

	while ( ! feof(stream) )
	{
		size_t n = fread(buf, 1, sizeof(buf), stream);
		data.append(buf, n);
	}

	pclose(stream);
	return data;
}

string getOutputString(string cmd)
{
	string data;
	char buf[2048];

	cmd += " 2>&1";

	FILE *stream = popen(cmd.c_str(), "r");

	if ( ! stream )
	{
		cerr << "getOutputString(): failed to run " << cmd << endl;
		exit(1);
	}

	while ( ! feof(stream) )
	{
		size_t n = fread(buf, 1, sizeof(buf), stream);
		data.append(buf, n);
	}

	pclose(stream);
	return data;
}

u8string encodeEnv(ifstream *ifs)
{
	u8string env;

	while ( *ifs )
	{
		char buf[512];
		ifs->getline(buf, sizeof(buf));

		if ( ifs->gcount() > 4 ) // min valid length 4, i.e "a=c"
		{
			u8string s((uint8_t*)buf);

			if ( ! s.find((uint8_t)'=') )
			{
				cerr << "invalid env variable assignment: '"
						<< buf << "' aborting!" << endl;
				exit(1);
			}

			env += s;
			env += (uint8_t)'\0';
		}
	}

	if ( env.length() > (ENV_SIZE - 1 - sizeof(uint32_t)) )
	{
		cerr << "environment size exceeded, aborting!" << endl;
		exit(1);
	}

	env.append(ENV_SIZE - env.length() - sizeof(uint32_t)
					, (uint8_t)'\0');

	Crc crc;

	crc.i = crc32(env);

	u8string crcS;
	crcS += crc.b[0];
	crcS += crc.b[1];
	crcS += crc.b[2];
	crcS += crc.b[3];

	env.insert(0, crcS);

	if ( env.length() != ENV_SIZE )
	{
		cerr << "encodeEnv(): incorrect env size, aborting!" << endl;
		exit(1);
	}

	return env;
}

string decodeEnvText(u8string env)
{
	u8string s(decodeEnv(env));
	return string((const char*)s.data(), s.length());
}

u8string decodeEnv(u8string env)
{
	if ( env.length() != ENV_SIZE )
	{
		cerr << "decodeEnv(): incorrect env size, aborting!" << endl;
		exit(1);
	}

	if ( env[ENV_SIZE-1] != '\0' )
	{
		cerr << "decodeEnv(): environment must be terminated with '\\0'." << endl;
		exit(1);
	}

	Crc crc;

	crc.i = crc32(0, env.data() + sizeof(uint32_t), env.length() - sizeof(uint32_t));

	if ( crc.b[0] != env[0]
			|| crc.b[1] != env[1]
			|| crc.b[2] != env[2]
			|| crc.b[3] != env[3] )
	{
		cerr << "decodeEnv: environment checksum mismatch." << endl;
		exit(1);
	}

	u8string ret;

	for ( size_t i = 4; i < env.length() - 5; ++i )
	{
		if ( env[i] != '\0' )
			ret += env[i];
		else
		{
			ret += '\n';

			if ( env[i+1] == '\0' )
				break;
		}
	}

	return ret;
}

u8string encodeNandRs(u8string env)
{
	u8string nandRs;

	for ( int i = 0; i < NAND_CHUNK_COUNT; ++i )
	{
		u8string chunk = env.substr(i * NAND_CHUNK_SIZE, NAND_CHUNK_SIZE);

		Oob oob;
		memset(oob.data.filler, -1, sizeof(oob.data.filler));

		for ( size_t i = 0; i < 4; ++i )
		{
			calculate_ecc_rs(chunk.data() + (i * ECC_CHUNK_SIZE), oob.data.ecc_buffers[i]);
		}

		chunk.append(&oob.b[0], sizeof(oob));
		nandRs.append(chunk);
	}

	if ( nandRs.length() != ENV_SIZE + (sizeof(Oob) * NAND_CHUNK_COUNT) )
	{
		cerr << "encodeNandRs(): incorrect nandRs size, aborting!" << endl;
		exit(1);
	}

	return nandRs;
}

u8string decodeNandRs(u8string nandRs)
{
	if ( nandRs.length() != ENV_SIZE + (sizeof(Oob) * NAND_CHUNK_COUNT) )
	{
		cerr << "decodeNandRs(): incorrect nandRs size, aborting!" << endl;
		exit(1);
	}

	u8string env;

	for ( size_t chunkNum = 0; chunkNum < NAND_CHUNK_COUNT; ++chunkNum )
	{
		size_t chunkStart = chunkNum * (NAND_CHUNK_SIZE + sizeof(Oob));
		size_t oobStart = chunkStart + NAND_CHUNK_SIZE;
		u8string chunk = nandRs.substr(chunkStart, NAND_CHUNK_SIZE);
		u8string oobS = nandRs.substr(oobStart, sizeof(Oob));
		Oob oob;

		oobS.copy(&oob.b[0], oobS.length());

		for ( size_t blockNum = 0; blockNum < 4; ++blockNum )
		{
			uint8_t eccChunk[ECC_CHUNK_SIZE];
			chunk.copy(eccChunk, ECC_CHUNK_SIZE, blockNum * ECC_CHUNK_SIZE);
			uint8_t computedEcc[ECC_SIZE];

			calculate_ecc_rs(eccChunk, computedEcc);

			if ( correct_data_rs( eccChunk, oob.data.ecc_buffers[blockNum], computedEcc) < 0 )
			{
				cerr << "decodeNandRs(): too many errors in block #" << blockNum
						<< " of chunk #" << chunkNum << endl;
				exit(1);
			}

			env.append(eccChunk, sizeof(eccChunk));
		}
	}

	if ( env.length() != ENV_SIZE )
	{
		cerr << "decodeNandRs(): incorrect env size, aborting!" << endl;
		exit(1);
	}

	return env;
}

/* ========================================================================
 * Table of CRC-32's of all single-byte values (made by make_crc_table)
 */
const uint32_t crc_table[256] = {
  0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
  0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
  0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
  0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
  0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
  0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
  0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
  0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
  0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
  0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
  0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
  0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
  0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
  0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
  0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
  0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
  0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
  0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
  0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
  0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
  0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
  0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
  0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
  0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
  0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
  0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
  0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
  0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
  0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
  0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
  0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
  0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
  0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
  0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
  0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
  0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
  0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
  0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
  0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
  0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
  0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
  0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
  0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
  0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
  0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
  0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
  0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
  0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
  0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
  0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
  0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
  0x2d02ef8dL
};

uint32_t crc32(const u8string &s)
{
	return crc32(0, s.data(), s.length());
}

#define DO1(buf) crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);
uint32_t crc32 (uint32_t crc, const uint8_t *buf, unsigned int len)
{
	crc = crc ^ 0xffffffffL;

	while ( len >= 8 )
	{
		DO8(buf);
		len -= 8;
	}

	if ( len )
	{
		do
		{
			DO1(buf);
		} while ( --len );
	}

	return crc ^ 0xffffffffL;
}

}; // anonymous namespace

