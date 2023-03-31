/*
 *
 * $Id: hardware-posix.h,v 1.6 2023/03/25 08:09:07 stefan Exp stefan $
 *
 *	Stefan's basic interpreter 
 *
 *	Playing around with frugal programming. See the licence file on 
 *	https://github.com/slviajero/tinybasic for copyright/left.
 *	(GNU GENERAL PUBLIC LICENSE, Version 3, 29 June 2007)
 *
 *	Author: Stefan Lenz, sl001@serverfabrik.de
 *
 *	Hardware definition file coming with basic.c aka TinybasicArduino.ino
 *
 *	Link to some of the POSIX OS features to mimic a microcontroller platform
 *	See hardware-arduino for more details on the interface.
 *
 *	Supported: filesystem, real time clock, (serial) I/O on the text console
 *	Not supported: radio, wire, SPI, MQTT
 *
 *	Partially supported: Wiring library on Raspberry PI
 *
 */

/* simulates SPI RAM, only test code, keep undefed if you don't want to do something special */
#undef SPIRAMSIMULATOR

/* use a serial port as printer interface - unfinished */
#define ARDUINOPRT

/* translate some ASCII control sequences to POSIX, tested on Mac */
#define POSIXTERMINAL

/* do we handle signals? */
#define HASSIGNALS

/* experimental code for non blocking I/0 - only supported on Mac and some MINGW*/
#define POSIXNONBLOCKING

/* try to use the frame buffer on linux rasbian */
#define POSIXFRAMEBUFFER

/* the MINGW variants */
#ifdef MINGW64
#define MINGW
#endif

/* frame bugger health check */ 
#ifndef RASPPI
#undef POSIXFRAMEBUFFER
#endif

#if ! defined(ARDUINO) && ! defined(__HARDWAREH__)
#define __HARDWAREH__ 

/* 
 * the system type and system capabilities
 */

#ifndef MSDOS
const char bsystype = SYSTYPE_POSIX;
#else
const char bsystype = SYSTYPE_MSDOS;
#endif

/* 
 * Arduino default serial baudrate and serial flags for the 
 * two supported serial interfaces. Set to 0 on POSIX OSes 
 */
const int serial_baudrate = 0;
const int serial1_baudrate = 0;
char sendcr = 0;
short blockmode = 0;

/*  handling time, remember when we started, needed in millis() */
struct timeb start_time;
void timeinit() { ftime(&start_time); }

/* starting wiring for raspberry */
void wiringbegin() {
#ifdef RASPPI
	wiringPiSetup();
#endif
}

/*
 * signal handling 
 */
#ifdef HASSIGNALS
#include <signal.h>
mem_t breaksignal = 0;

/* simple signal handler */
void signalhandler(int sig){
	breaksignal=1;
	signal(SIGINT, signalhandler);
}

/* activate signal handling */
void signalon() {
	signal(SIGINT, signalhandler);
}

/* deactivate signal handling unused and not yet done*/
void signaloff() {}

#endif

/*
 * helper functions OS, heuristic on how much memory is 
 * available in BASIC
 */
long freememorysize() {
#ifdef MSDOS
	return 48000;
#else
	return 65536;
#endif  
}

long freeRam() {
	return freememorysize();
}

/* 
 * the sleep and restart functions
 */
void restartsystem() {exit(0);}
void activatesleep(long t) {}


/* 
 * start the SPI bus 
 */
void spibegin() {}


/*
 * DISPLAY driver code section, the hardware models define a set of 
 * of functions and definitions needed for the display driver. These are 
 *
 * dsp_rows, dsp_columns: size of the display
 * dspbegin(), dspprintchar(c, col, row), dspclear()
 * 
 * All displays which have this functions can be used with the 
 * generic display driver below.
 * 
 * Graphics displays need to implement the functions 
 * 
 * rgbcolor(), vgacolor()
 * plot(), line(), rect(), frect(), circle(), fcircle() 
 * 
 * Color is currently either 24 bit or 4 bit 16 color vga.
 */
const int dsp_rows=0;
const int dsp_columns=0;
void dspsetupdatemode(char c) {}
void dspwrite(char c){}
void dspbegin() {}
int dspstat(char c) {return 0; }
char dspwaitonscroll() { return 0; }
char dspactive() {return 0; }
void dspsetscrollmode(char c, short l) {}
void dspsetcursor(short c, short r) {}

#ifndef POSIXFRAMEBUFFER
/* these are the graphics commands */
void rgbcolor(int r, int g, int b) {}
void vgacolor(short c) {}
void plot(int x, int y) {}
void line(int x0, int y0, int x1, int y1)   {}
void rect(int x0, int y0, int x1, int y1)   {}
void frect(int x0, int y0, int x1, int y1)  {}
void circle(int x0, int y0, int r) {}
void fcircle(int x0, int y0, int r) {}

/* stubs for the vga code part analogous to ESP32 */
void vgabegin(){}
void vgawrite(char c){}
#else
/* 
 * This is the first draft of the linux framebuffer code 
 * currently very raw, works only if the framebuffer is 24 bit 
 * very few checks, all kind of stuff can go wrong here.
 * 
 * Main ideas and some part of the code came from this
 * article https://www.mikrocontroller.net/topic/379335
 * by Andy W.
 * 
 * Bresenham's algorithm came from the Wikipedia article 
 * and this very comprehensive discussion
 * http://members.chello.at/~easyfilter/bresenham.html
 * by Alois Zingl from the Vienna Technikum. I also recommend
 * his thesis: http://members.chello.at/%7Eeasyfilter/Bresenham.pdf
 * 
 */
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <string.h>

/* 'global' variables to store screen info */
char *framemem = 0;
int framedesc = 0;

/* info from the frame buffer itself */
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
struct fb_var_screeninfo orig_vinfo;

/* the color variable of the frame buffer */
long framecolor = 0xffffff;
int  framevgacolor = 0x0f;
long framescreensize = 0;


/* prepare the framebuffer device */
void vgabegin() {

/* see if we can open the framebuffer device */
	framedesc = open("/dev/fb0", O_RDWR);
	if (!framedesc) {
		printf("** error opening frame buffer \n");
		return;
	} 

/* now get the variable info of the screen */
	if (ioctl(framedesc, FBIOGET_VSCREENINFO, &vinfo)) {
		printf("** error reading screen information \n");
		return;
	}
	printf("** detected screen %dx%d, %dbpp \n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

/* BASIC currently does 24 bit color only */
	memcpy(&orig_vinfo, &vinfo, sizeof(struct fb_var_screeninfo)); 
	vinfo.bits_per_pixel = 24; 
	if (ioctl(framedesc, FBIOPUT_VSCREENINFO, &vinfo)) {
		printf("** error setting variable information \n");
		return;
	}

/* get the fixed information of the screen */
	if (ioctl(framedesc, FBIOGET_FSCREENINFO, &finfo)) {
		printf("Error reading fixed information.\n");
		return;
	}

/* now ready to memory map the screen - evil, we assume 24 bit without checking */
	framescreensize = 3 * vinfo.xres * vinfo.yres;  
	framemem = (char*)mmap(0, framescreensize, PROT_READ | PROT_WRITE, MAP_SHARED, framedesc, 0);
	if ((int)framemem == -1) {
		printf("** error failed to mmap.\n");
		framemem=0;
		return;
	}

/* if all went well we have valid non -1 framemem and can continue */
}

/* this function does not exist in the ESP32 world because we don't care there */
void vgaend() {
	if ((int)framemem) munmap(framemem, framescreensize); 
	if (ioctl(framedesc, FBIOPUT_VSCREENINFO, &orig_vinfo)) {
		printf("** error re-setting variable information \n");
	}
	close(framedesc);
}

/* set the color variable */
void rgbcolor(int r, int g, int b) {
	framecolor = (((long)r << 16) & 0x00ff0000) | (((long)g << 8) & 0x0000ff00) | ((long)b & 0x000000ff);
}

/* this is taken from the Arduino TFT code */
void vgacolor(short c) {
  short base=128;
  framevgacolor=c;
  if (c==8) { rgbcolor(64, 64, 64); return; }
  if (c>8) base=255;
  rgbcolor(base*(c&1), base*((c&2)/2), base*((c&4)/4));  
}

/* plot directly into the framebuffer */
void plot(int x, int y) {
	unsigned long pix_offset;

/* is everything in range, no error here */
	if (x < 0 || y < 0 || x >= vinfo.xres || y >= vinfo.yres) return; 

/* find the memory location - currently only 24 bit supported */
	pix_offset = 3 * x + y * finfo.line_length;
	if (pix_offset < 0 || pix_offset+2 > framescreensize) return;

/* write to the buffer */
	*((char*)(framemem + pix_offset  )) = (unsigned char)(framecolor         & 0x000000ff);
	*((char*)(framemem + pix_offset+1)) = (unsigned char)((framecolor >>  8) & 0x000000ff);
	*((char*)(framemem + pix_offset+2)) = (unsigned char)((framecolor >> 16) & 0x000000ff);
}

/* Bresenham's algorith from Wikipedia */
void line(int x0, int y0, int x1, int y1) {
	int dx, dy, sx, sy;
	int error, e2;
	
	dx=abs(x0-x1);
	sx=x0 < x1 ? 1 : -1;
	dy=-abs(y1-y0);
	sy=y0 < y1 ? 1 : -1;
	error=dx+dy;

	while(1) {
		plot(x0, y0);
		if (x0 == x1 && y0 == y1) break;
		e2=2*error;
		if (e2 > dy) {
			if (x0 == x1) break;
			error=error+dy;
			x0=x0+sx;
		}
		if (e2 <= dx) {
			if (y0 == y1) break;
			error=error+dx;
			y0=y0+sy;
		}
	}
}

/* rects could also be drawn with hline and vline */
void rect(int x0, int y0, int x1, int y1) {
	line(x0, y0, x1, y0);
	line(x1, y0, x1, y1);
	line(x1, y1, x0, y1);
	line(x0, y1, x0, y0); 
}

/* filled rect, also just using line right now */
void frect(int x0, int y0, int x1, int y1) {
	int dx, sx;
	int x;
	sx=x0 < x1 ? 1 : -1;
	for(x=x0; x != x1; x=x+sx) line(x, y0, x, y1);
}

/* Bresenham for circles, based on Alois Zingl's work */
void circle(int x0, int y0, int r) {
	int x, y, err;
	x=-r;
	y=0; 
	err=2-2*r;
	do {
		plot(x0-x, y0+y);
		plot(x0-y, y0-x);
		plot(x0+x, y0-y);
		plot(x0+y, y0+x);
		r=err;
		if (r <= y) err+=++y*2+1;
		if (r > x || err > y) err+=++x*2+1;
	} while (x < 0);
}

/* for filled circles draw lines instead of points */
void fcircle(int x0, int y0, int r) {
	int x, y, err;
	x=-r;
	y=0; 
	err=2-2*r;
	do {
		line(x0-x, y0+y, x0+x, y0+y);
		line(x0+x, y0-y, x0-x, y0-y);
		r=err;
		if (r <= y) err+=++y*2+1;
		if (r > x || err > y) err+=++x*2+1;
	} while (x < 0);
}

/* not needed really, now, later yes ;-) */
void vgawrite(char c) {}
#endif

/* 
 * Keyboard code stubs
 * keyboards can implement 
 * 	kbdbegin()
 * they need to provide
 * 	kbdavailable(), kbdread(), kbdcheckch()
 * the later is for interrupting running BASIC code
 */
void kbdbegin() {}
int kbdstat(char c) {return 0; }
char kbdavailable(){ return 0;}
char kbdread() { return 0;}
char kbdcheckch() { return 0;}

/* vt52 code stubs */
mem_t vt52avail() {return 0;}
char vt52read() { return 0; }

/* Display driver would be here, together with vt52 */


/* 
 * Real Time clock code 
 */
#define HASCLOCK

void rtcbegin() {}

short rtcget(short i) {
	struct timeb thetime;
	struct tm *ltime;
	ftime(&thetime);
	ltime=localtime(&thetime.time);
	switch (i) {
		case 0: 
			return ltime->tm_sec;
		case 1:
			return ltime->tm_min;
		case 2:
			return ltime->tm_hour;
		case 3:
			return ltime->tm_wday;
		case 4:
			return ltime->tm_mday;
		case 5:
			return ltime->tm_mon+1;
		case 6:
			return ltime->tm_year-100;
		default:
			return 0;
	}
}

void rtcset(uint8_t i, short v) {}


/* 
 * Wifi and MQTT code 
 */
void netbegin() {}
char netconnected() { return 0; }
void mqttbegin() {}
int mqttstat(char c) {return 0; }
int  mqttstate() {return -1;}
void mqttsubscribe(char *t) {}
void mqttsettopic(char *t) {}
void mqttouts(char *m, short l) {}
void mqttins(char *b, short nb) { z.a=0; };
char mqttinch() {return 0;};

/* 
 *	EEPROM handling, these function enable the @E array and 
 *	loading and saving to EEPROM with the "!" mechanism
 *	a filesystem based dummy
 */ 
signed char eeprom[EEPROMSIZE];
void ebegin(){ 
	int i;
	FILE* efile;
	for (i=0; i<EEPROMSIZE; i++) eeprom[i]=-1;
	efile=fopen("eeprom.dat", "r");
	if (efile) fread(eeprom, EEPROMSIZE, 1, efile);
}

void eflush(){
	FILE* efile;
	efile=fopen("eeprom.dat", "w");
	if (efile) fwrite(eeprom, EEPROMSIZE, 1, efile);
	fclose(efile);
}

address_t elength() { return EEPROMSIZE; }
void eupdate(address_t a, short c) { if (a>=0 && a<EEPROMSIZE) eeprom[a]=c; }
short eread(address_t a) { if (a>=0 && a<EEPROMSIZE) return eeprom[a]; else return -1;  }

/* 
 *	the wrappers of the arduino io functions, to avoid 
 */	
#ifndef RASPPI
void aread(){ return; }
void dread(){ return; }
void awrite(number_t p, number_t v){}
void dwrite(number_t p, number_t v){}
void pinm(number_t p, number_t m){}
#else
void aread(){ push(analogRead(pop())); }
void dread(){ push(digitalRead(pop())); }
void awrite(number_t p, number_t v){
	if (v >= 0 && v<256) analogWrite(p, v);
	else error(EORANGE);
}
void dwrite(number_t p, number_t v){
	if (v == 0) digitalWrite(p, LOW);
	else if (v == 1) digitalWrite(p, HIGH);
	else error(EORANGE);
}
void pinm(number_t p, number_t m){
	if (m>=0 && m<=1) pinMode(p, m);
	else error(EORANGE); 
}
#endif


/* we need to to millis by hand except for RASPPI with wiring */
#if ! defined(RASPPI)
unsigned long millis() { 
	struct timeb thetime;
	ftime(&thetime);
	return (thetime.time-start_time.time)*1000+(thetime.millitm-start_time.millitm);
}
#endif

void bpulsein() { pop(); pop(); pop(); push(0); }
void bpulseout(short a) { while (a--) pop(); }
void btone(short a) { pop(); pop(); if (a == 3) pop(); }

/* the POSIX code has no yield as it runs on an OS */
void yieldfunction() {}
void longyieldfunction() {}
void yieldschedule() {}

/* 
 *	The file system driver - all methods needed to support BASIC fs access
 *	MSDOS to be done 
 *
 * file system code is a wrapper around the POSIX API
 */
void fsbegin(char v) {}
#define FILESYSTEMDRIVER
FILE* ifile;
FILE* ofile;
#ifndef MSDOS
DIR* root;
struct dirent* file; 
#else
void* root;
void* file;
#endif 

/* POSIX OSes always have filesystems */
int fsstat(char c) {return 1; }

/*
 *	File I/O function on an Arduino
 * 
 * filewrite(), fileread(), fileavailable() as byte access
 * open and close is handled separately by (i/o)file(open/close)
 * only one file can be open for write and read at the same time
 */
void filewrite(char c) { if (ofile) fputc(c, ofile); else ert=1;}

char fileread(){
	char c;
	if (ifile) c=fgetc(ifile); else { ert=1; return 0; }
	if (cheof(c)) ert=-1;
	return c;
}

char ifileopen(const char* filename){
	ifile=fopen(filename, "r");
	return (int) ifile;
}

void ifileclose(){
	if (ifile) fclose(ifile);
	ifile=0;	
}

char ofileopen(char* filename, const char* m){
	ofile=fopen(filename, m);
	return (int) ofile; 
}

void ofileclose(){ 
	if (ofile) fclose(ofile); 
	ofile=0;
}

int fileavailable(){ return !feof(ifile); }

/*
 * directory handling for the catalog function
 * these methods are needed for a walkthtrough of 
 * one directory
 *
 * rootopen()
 * while rootnextfile()
 * 	if rootisfile() print rootfilename() rootfilesize()
 *	rootfileclose()
 * rootclose()
 */
#ifdef MSDOS
#include <dos.h>
#include <dir.h>
struct ffblk *bffblk; 
#endif

void rootopen() {
#ifndef MSDOS
	root=opendir ("./");
#else 
	(void) findfirst("*.*", bffblk, 0);
#endif
}

int rootnextfile() {
#ifndef MSDOS
  file = readdir(root);
  return (file != 0);
#else 
  return (findnext(bffblk) == 0);
#endif
}

int rootisfile() {
#if !defined(MSDOS) && !defined(MINGW64)
  return (file->d_type == DT_REG);
#else
  return 1;
#endif
}

const char* rootfilename() { 
#ifndef MSDOS
  return (file->d_name);
#else
  return (bffblk->ff_name);
#endif  
}

long rootfilesize() { 
#ifndef MSDOS
	return 0;
#else
	return (bffblk->ff_fsize);
#endif
}

void rootfileclose() {}
void rootclose(){
#ifndef MSDOS
  (void) closedir(root);
#endif  
}

/*
 * remove method for files
 */
void removefile(char *filename) {
	remove(filename);
}

/*
 * formatting for fdisk of the internal filesystems
 */
void formatdisk(short i) {
	outsc("Format not implemented on this platform\n");
}

/*
 * Primary serial code, if NONBLOCKING is set, 
 * platform dependent I/O is used. This means that 
 * UNIXes use fcntl() to implement a serialcheckch
 * and MSDOS as well als WIndows use kbhit(). 
 * This serves only to interrupt programs with 
 * BREAKCHAR at the moment. 
 */
#ifdef POSIXNONBLOCKING
#if !defined(MSDOS) && !defined(MINGW)
#include <fcntl.h>

/* we need to poll the serial port in non blocking mode 
		this slows it down so that we don't block an entire core 
		read speed here is one character per millisecond which
		is 8000 baud, no one can type that fast but tedious when
		from stdin */
/*
void freecpu() {
	struct timespec intervall;
	struct timespec rtmp;
	intervall.tv_sec=0;
	intervall.tv_nsec=1000000;
	nanosleep(&intervall, &rtmp);
}
*/

/* for non blocking I/O try to modify the stdin file descriptor */
void serialbegin() {
/* we keep I/O mostly blocking here */
/*
 fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
*/
}

/* get and unget the character in a non blocking way */
short serialcheckch(){ 
 	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	int ch=getchar();
	ungetc(ch, stdin);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~O_NONBLOCK);
	return ch;
}

/* check EOF, don't use feof()) here */
short serialavailable() { 
 if (cheof(serialcheckch())) return 0; else return 1;
}

/* two versions of serialread */
char serialread() { 
	char ch;
/* blocking to let the OS handle the wait - this means: no call to byield() in interaction */
	ch=getchar();
	return ch;
/* this is the code that waits - calls byield() often just like on the Arduino */
/*
	while (cheof(serialcheckch())) { byield(); freecpu(); }
	return getchar(); 
*/
}

/* flushes the serial code in non blocking mode */
void serialflush() {
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	while (!cheof(getchar()));
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~O_NONBLOCK);
}
#else
/* the non blocking MSDOS and MINGW code */
#include <conio.h>
/* we go way back in time here and do it like DOS did it */ 
void serialbegin(){}

/* we go through the terminal on read */
char serialread() { 
	return getchar();
}

/* check if a key is hit, get it and return it */
short serialcheckch(){ 
	if (kbhit()) return getch();
}

/* simple version */
short serialavailable() {
	return 1;
}

/* simple version */
void serialflush() { }
#endif
#else 
/* the blocking code only uses puchar and getchar */
void serialbegin(){}
char serialread() { return getchar(); }
short serialcheckch(){ return 1; }
short serialavailable() { return 1; }
void serialflush() {}
#endif

int serialstat(char c) {
	if (c == 0) return 1;
  if (c == 1) return serial_baudrate;
  return 0;
}

void serialwrite(char c) { 
#ifdef HASMSTAB
	if (c > 31) charcount+=1;
	if (c == 10) charcount=0;
#endif
#ifdef POSIXTERMINAL
	switch (c) {
/* form feed is clear screen - compatibility with Arduino code */
		case 12:
			putchar(27); putchar('['); /* CSI */
			putchar('2'); putchar('J');
/* home sequence in the arduino code */
		case 2: 
			putchar(27); putchar('['); /* CSI */
			putchar('H');
			break;
		default:
			putchar(c);
	}
#else
	putchar(c); 
#endif
}


/*
 * reading from the console with inch 
 * this mixes interpreter levels as inch is used here 
 * this code needs to go to the main interpreter section after 
 * thorough rewrite
 */
void consins(char *b, short nb) {
	char c;
	
	z.a=1;
	while(z.a < nb) {
		c=inch();
		if (c == '\r') c=inch();
		if (c == '\n' || cheof(c)) { /* terminal character is either newline or EOF */
			break;
		} else {
			b[z.a++]=c;
		} 
	}
	b[z.a]=0x00;
	z.a--;
	b[0]=(unsigned char)z.a;
}

/* 
 * handling the second serial interface - only done on Mac so far 
 * test code
 * 
 * Tried to learn from https://www.pololu.com/docs/0J73/15.5
 *
 */
#ifdef ARDUINOPRT
#include <fcntl.h>
#if !defined(MSDOS) && !defined(MINGW)
#include <termios.h>
#endif

/* the file name of the printer port */
int prtfile;

/* the buffer to read one character */
char prtbuf = 0;


void prtbegin() {}

char prtopen(char* filename, int mode) {
#if !defined(MSDOS) && !defined(MINGW)

/* try to open the device file */
	prtfile=open(filename, O_RDWR | O_NOCTTY);
	if (prtfile == -1) {
		perror(filename);
		return 0;
	} 

/* get rid of garbage */
	tcflush(prtfile, TCIOFLUSH);

/* configure the device */
	struct termios opt;
	(void) tcgetattr(prtfile, &opt);


/* raw terminal settings
  opt.c_iflag &= ~(INLCR | IGNCR | ICRNL | IXON | IXOFF);
  opt.c_oflag &= ~(ONLCR | OCRNL);
  opt.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
*/

/* timeout settings on read 100ms, read every character */
  opt.c_cc[VTIME] = 1;
  opt.c_cc[VMIN] = 0;

/* set the baudrate */
  switch (mode) {
  	case 9600: 
			cfsetospeed(&opt, B9600);
			break;
		default:
			cfsetospeed(&opt, B9600);
			break;
  }
  cfsetispeed(&opt, cfgetospeed(&opt));

/* set the termin attributes */
  tcsetattr(prtfile, TCSANOW, &opt);
#endif

	return 1;
}

void prtclose() {
	if (prtfile) close(prtfile);
}

int prtstat(char c) {return 1; }
void prtset(int s) {}

/* write the characters byte by byte */
void prtwrite(char c) {
	int i=write(prtfile, &c, 1);
	if (i != 1) ert=1;
}

/* read just one byte, map no bytes to EOF = -1 */
char prtread() {
	char c;

/* something in the buffer? return it! */
	if (prtbuf) {
		c=prtbuf;
		prtbuf=0;
	} else {
/* try to read */
		int i=read(prtfile, &c, 1);
		if (i < 0) {
			ert=1;
			return 0;
		} 
		if (i == 0) return -1;
	}
	return c;
}

/* not yet implemented */
short prtcheckch(){ 
	if (!prtbuf) { /* try to read */
		int i=read(prtfile, &prtbuf, 1);
		if (i <= 0) prtbuf=0;
	}
	return prtbuf; 
}

short prtavailable(){ 
	return prtcheckch()!=0; 
}

#else
void prtbegin() {}
int prtstat(char c) {return 0; }
void prtset(int s) {}
void prtwrite(char c) {}
char prtread() {return 0;}
short prtcheckch(){ return 0; }
short prtavailable(){ return 0; }
#endif


/* 
 * The wire code 
 */ 
void wirebegin() {}
int wirestat(char c) {return 0; }
void wireopen(char s, char m) {}
void wireins(char *b, uint8_t l) { b[0]=0; z.a=0; }
void wireouts(char *b, uint8_t l) {}
short wireavailable() { return 1; }

/* 
 *	Read from the radio interface, radio is always block 
 *	oriented. 
 */
int radiostat(char c) {return 0; }
void radioset(int s) {}
void radioins(char *b, short nb) { b[0]=0; b[1]=0; z.a=0; }
void radioouts(char *b, short l) {}
void iradioopen(char *filename) {}
void oradioopen(char *filename) {}
short radioavailable() { return 0; }

/* Arduino sensors */
void sensorbegin() {}
number_t sensorread(short s, short v) {return 0;};

/*
 * Experimental code to simulate 64kb SPI SRAM modules
 * 
 * currently used to test the string code of the mem 
 * interface
 *
 */

#ifdef SPIRAMSIMULATOR
#define USEMEMINTERFACE

static mem_t* spiram;

/* the RAM begin method sets the RAM to byte mode */
address_t spirambegin() {
  spiram=(mem_t*)malloc(65536);
  if (maxnum>32767) return 65534; else return 32766;  
}

/* the simple unbuffered byte write, with a cast to signed char */
void spiramrawwrite(address_t a, mem_t c) {spiram[a]=c;}

/* the simple unbuffered byte read, with a cast to signed char */
mem_t spiramrawread(address_t a) {return spiram[a];}

/* the buffers calls, also only simulated here */

void spiram_rwbufferwrite(address_t a, mem_t c) {spiram[a]=c;}

mem_t spiram_rwbufferread(address_t a) {return spiram[a];}

mem_t spiram_robufferread(address_t a) {return spiram[a];}



/* to handle strings in SPIRAM situations two more buffers are needed 
 * they store intermediate results of string operations. The buffersize 
 * limits the maximum string length indepents of how big strings are set
 */
#define SPIRAMSBSIZE 128
char spistrbuf1[SPIRAMSBSIZE];
char spistrbuf2[SPIRAMSBSIZE];
#endif

/* stub for events */
#ifdef HASEVENTS
mem_t enableevent(mem_t pin) {
	int i;
  if ((i=eventindex(pin))<0) return 0; 
  else {
  	eventlist[i].enabled=1; 
  	return 1;
  }
}
void disableevent(mem_t pin) {}
#endif


#endif

