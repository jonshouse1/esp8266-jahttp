/*  jahttp.c - Memory file system based http server

    Version 0,9, May 2016 Jonathan Andrews (jon @ jonshouse.co.uk )

    based on tcp_server.c by Tom Trebisky:  http://cholla.mmto.org/esp8266/OLD/sdk/tcp_server.c
    His original notes: http://cholla.mmto.org/esp8266/OLD/sdk/

    Flash memory file system by the genious that is Charles Lohr 
    Original link: https://github.com/cnlohr/esp8266ws2812i2s
*/    

#define INDEX_INSTEAD_OF_NOTFOUND				// uncomment to output index.html rather than a 404 message
#define MAX_CONNECTIONS			16			// Hope all of these are not active
#define LED_ACTIVITY			4
//#define DEBUG							// uncomment for lots of serial output


#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"
#include "espconn.h"
#include "mem.h"
#include "mfs.h"
#include "mfs_filecount.h"
#include <gpio.h>


#define HT_STATE_IDLE			0
#define HT_STATE_CLOSE			1
#define HT_STATE_SENDING_FILELIST	2
#define HT_STATE_SENDING_FILE		3
#define HT_STATE_NOTFOUND		4

#define HT_CONTENT_TEXTHTML		0
#define HT_CONTENT_IMAGEJPEG		1
#define HT_CONTENT_IMAGEPNG		2
#define HT_CONTENT_UNKNOWN		3
#define HT_CONTENT_TEXTPLAIN		4


int ctest=0x1122;
int has_index_html; 
// We use an array of this to hold all the state for a connection
struct connection
{
	int	connected;									// 'socket' is active
	int 	remote_port;
	uint8 	remote_ip[4];
	int  	ht_content_type;			
	int  	ht_state;
	int  	ht_count;									// Which chunk of file or output
	char 	ht_filename[32];
	char 	ht_fh;										// File handle
	struct  MFSFileInfo mfs_fileinfo;
};
struct __attribute__ ((aligned (4))) connection connections[MAX_CONNECTIONS];			// Array of connections, index in the table acts as a handle


#ifdef LED_ACTIVITY
static volatile os_timer_t led_off_timer;
void ICACHE_FLASH_ATTR led_off_timer_cb( void )
{
    GPIO_OUTPUT_SET(LED_ACTIVITY,1);								// LED off
}
#endif



#ifdef DEBUG
static volatile os_timer_t sh_timer;
void ICACHE_FLASH_ATTR sh_timer_cb( void )
{
	int i;
	os_printf("%c[2J;%c[1;1H",27,27);
	
	for (i=0;i<MAX_CONNECTIONS;i++)
	{
		if (connections[i].connected==TRUE)
		{
			os_printf("%d\t%d%d%d%d\t%d\n",i,connections[i].remote_ip[0],
							 connections[i].remote_ip[1],
							 connections[i].remote_ip[2],
							 connections[i].remote_ip[3]);
		}
	}
}

#endif


// Due to a rather poor connection API we dont seem to have a unique handle for each connection made, we have to make a handle using the ip and port
// of the host.  We might as well store all the state we need in the handle table.
#define HANDLE_REMOVE		1
#define HANDLE_CREATE		2
#define HANDLE_LOOKUP		3
// Returns -1 on error
// For each connection the remote_port and its ip identify it as unique.  Turn these ip/port pairs into a handle
int ICACHE_FLASH_ATTR connectionhandle(void *arg, int action)
{
	struct espconn *conn = arg;
	int i;
	int f;


	// Did not exist in table, optionally try and add it
	if (action==HANDLE_CREATE)
	{
		for (i=0;i<MAX_CONNECTIONS;i++)
		{
			if (connections[i].connected!=TRUE)					// this line is not in use ?
			{
				connections[i].connected=TRUE;					// mark it as in use now
				connections[i].remote_port =conn->proto.tcp->remote_port;
				connections[i].remote_ip[0]=conn->proto.tcp->remote_ip[0];
				connections[i].remote_ip[1]=conn->proto.tcp->remote_ip[1];
				connections[i].remote_ip[2]=conn->proto.tcp->remote_ip[2];
				connections[i].remote_ip[3]=conn->proto.tcp->remote_ip[3];
				os_printf("Create handle %d for %d.%d.%d.%d port %d\n",i,connections[i].remote_ip[0],
											 connections[i].remote_ip[1],
											 connections[i].remote_ip[2],
											 connections[i].remote_ip[3],
											 connections[i].remote_port);
				return(i);							// tell caller the table index (handle)
			}
		}
	}


	// Look for an existing line in the table that matches the passed ip and port, update the table entry to reflect new state
	f=-1;
	for (i=0;i<MAX_CONNECTIONS;i++)
	{
		if ( (connections[i].connected == TRUE ) && 
		     (connections[i].remote_port  == conn->proto.tcp->remote_port) && 
		     (connections[i].remote_ip[0] == conn->proto.tcp->remote_ip[0] ) && 
		     (connections[i].remote_ip[1] == conn->proto.tcp->remote_ip[1] ) && 
		     (connections[i].remote_ip[2] == conn->proto.tcp->remote_ip[2] ) && 
		     (connections[i].remote_ip[3] == conn->proto.tcp->remote_ip[3] ) )
		{
			f=i;									// Found entry at this index
			connections[i].connected=TRUE;
			connections[i].remote_port =conn->proto.tcp->remote_port;
			connections[i].remote_ip[0]=conn->proto.tcp->remote_ip[0];
			connections[i].remote_ip[1]=conn->proto.tcp->remote_ip[1];
			connections[i].remote_ip[2]=conn->proto.tcp->remote_ip[2];
			connections[i].remote_ip[3]=conn->proto.tcp->remote_ip[3];
			if (action==HANDLE_LOOKUP)						// caller just wants the handle
				return(i);
			break;
		}
	}

	if (f<0)
		return (-1);									// Did not find table index from ip/port 

	if ( action==HANDLE_REMOVE )
	{
		connections[f].connected=FALSE;
		connections[f].ht_state=HT_STATE_IDLE;
		connections[f].ht_count=0;
		connections[f].ht_content_type=0;
		connections[f].ht_filename[0]=0;
		os_printf("Releasing handle %d\n",f);
		return (f);									// return the handle just invalidated
	}
} 



char url[65];


void show_ip ( void );

void
show_mac ( void )
{
    unsigned char mac[6];

    wifi_get_macaddr ( STATION_IF, mac );
    os_printf ( "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
}

/* Could require up to 16 bytes */
char *
ip2str ( char *buf, unsigned char *p )
{
    os_sprintf ( buf, "%d.%d.%d.%d", p[0], p[1], p[2], p[3] );
    return buf;
}

#ifdef notdef
/* Could require up to 16 bytes */
char *
ip2str_i ( char *buf, unsigned int ip )
{
    int n1, n2, n3, n4;

    n1 = ip & 0xff;
    ip >>= 8;
    n2 = ip & 0xff;
    ip >>= 8;
    n3 = ip & 0xff;
    ip >>= 8;
    n4 = ip & 0xff;
    os_sprintf ( buf, "%d.%d.%d.%d", n1, n2, n3, n4 );
    return buf;
}
#endif

void
show_ip ( void )
{
    struct ip_info info;
    char buf[16];

    wifi_get_ip_info ( STATION_IF, &info );
    // os_printf ( "IP: %08x\n", info.ip );
    // os_printf ( "IP: %s\n", ip2str_i ( buf, info.ip.addr ) );
    os_printf ( "IP: %s\n", ip2str ( buf, (char *) &info.ip.addr ) );
}



void ICACHE_FLASH_ATTR send_header (void *arg, int ht_content_type, int length, int rewrite)
{
	char header[1024];
	char st[32];
	//os_printf("sending header, ht_content_type=%d, length=%d\n",ht_content_type, length);
if (ctest!=0x1122)
	os_printf("CORRUPTION CORRUPTION CORRUPTION\n");

	os_sprintf(header,"HTTP/1.1 200 OK\nServer: ESP\n");
	if (rewrite==TRUE)
	{
		os_sprintf(st,"Refresh: 0; url=/\n");							// force browser to /
		strcat(header,st); 
	}

	if (length>0)											// if we know then say so
	{
		os_sprintf(st,"Content-Length: %d\n",length);	
		strcat(header,st); 
	}

	os_sprintf(st,"Content-Type: ");      
	strcat(header,st); 
	switch (ht_content_type)
	{
		case HT_CONTENT_TEXTHTML:	os_sprintf(st,"text/html\n");	break;
		case HT_CONTENT_IMAGEJPEG:	os_sprintf(st,"image/jpeg\n");	break;
		case HT_CONTENT_IMAGEPNG:	os_sprintf(st,"image/png\n");	break;
		case HT_CONTENT_UNKNOWN:	os_sprintf(st,"unknown\n");	break;
		case HT_CONTENT_TEXTPLAIN:	os_sprintf(st,"text/plain\n");	break;
	}
	strcat(header,st); 
	os_sprintf(st,"\n");      
	strcat(header,st); 
	espconn_send ( arg, (uint8 *)&header, strlen(header));
	strcat(header,"Connection: close\n");								// Tell browser to close socket when done
}


int ICACHE_FLASH_ATTR detect_content_type(char * ht_filename)
{
	int i;
	char lastthree[4];

	i=strlen(ht_filename);
	if (i>3)
	{
		lastthree[0]=tolower(ht_filename[i-3]);
		lastthree[1]=tolower(ht_filename[i-2]);
		lastthree[2]=tolower(ht_filename[i-1]);
		lastthree[3]=0;

		//os_printf("lastthree=[%s]\n",lastthree);
		if (strcmp(lastthree,"jpg")==0)
			return HT_CONTENT_IMAGEJPEG;
		if (strcmp(lastthree,"png")==0)
			return HT_CONTENT_IMAGEPNG;
		if (strcmp(lastthree,"tml")==0)
			return HT_CONTENT_TEXTHTML;
		if (strcmp(lastthree,"htm")==0)
			return HT_CONTENT_TEXTHTML;
		if ( (strcmp(lastthree+1,".c")==0) | (strcmp(lastthree+1,".h")==0) )
			return HT_CONTENT_TEXTPLAIN;
	}
	return HT_CONTENT_TEXTHTML;				// maybe change to unknown
}




// Send one chunk of a file
void ICACHE_FLASH_ATTR send_file_chunk(void *arg)
{
    int h;
    char __attribute__ ((aligned (4))) asector[MFS_SECTOR];
    int bytesleft=0;
    int cs=MFS_SECTOR;
    int r;

#ifdef LED_ACTIVITY
    GPIO_OUTPUT_SET(LED_ACTIVITY,0);									// LED on
#endif

    h=connectionhandle(arg, HANDLE_LOOKUP);								// Find our connection details
    if (h<0)
	return;

    //os_printf("send_file_chunk [%s] %d\n",connections[h].filename,connections[h].ht_count);
    if (connections[h].ht_count==0)
    {
        connections[h].ht_fh=MFSOpenFile(connections[h].ht_filename, &connections[h].mfs_fileinfo);
	if (connections[h].ht_fh<0)
	{
		connections[h].ht_state=HT_STATE_CLOSE;							// Hangup socket
		os_printf("send_file_chunk error\n");
		return;
	}
    }

    //Bug: We are sending an entire sector as the last send regardless of where the file actually ended.
    bytesleft = MFSReadSector( &asector[0], &connections[h].mfs_fileinfo );				// It always reads MFS_SECTOR size chunks
    if (bytesleft<=0)
	connections[h].ht_state=HT_STATE_CLOSE;
    espconn_send ( arg, (uint8 *)&asector[0], cs);							// send next chunk of tcp data
}



// This is not the last word in code quaility, it is possible a malformed URL could crash this although
// WDT would fire and it would reboot so life goes on !
void ICACHE_FLASH_ATTR tcp_receive_data ( void *arg, char *buf, unsigned short len )
{
    int h;
    int i;
    int u;
    int getat=0;
    char st[65];
    int fsize;

    h=connectionhandle(arg, HANDLE_LOOKUP);								// Find our connection details
    if (h<0)
  	return;

    os_printf ( "TCP receive data: %d bytes\n", len );
    //espconn_send ( arg, buf, len );

    // The string we are after has the form "GET / HTTP/1.1"
    //os_printf("got [%s]\n",buf);
    getat=-1;
    for (i=0;i<len-4;i++)
    {
	if ( (buf[i]=='G') && (buf[i+1]=='E') && (buf[i+2]=='T') )
	{
		getat=i;	
		break;
	}
    }

    //os_printf("'GET' is at %d\n",getat);
    bzero(&url,sizeof(url));
    if (getat>=0)
    {
  	i=getat+5;										// Copy text after GET
    	while ( (i<len) && (buf[i]!=10) && (buf[i]!=13) && (i<sizeof(url)) )
		url[u++]=buf[i++];
	while (url[u]!=' ' && u>0)
		u--;										// now walk backwards looking for a space
	url[u]=0;										// shorten string, remove HTTP/1.1
	os_printf("URL=[%s]\n",url);

	// Match the URL with a file
	connections[h].ht_count=0;
        if ( (url[0]==0) || (url[0]=='?') )							// url line is blank or a ? on its own
	{
		//os_printf("short or zero url, has_index_html=%d T=%d\n",has_index_html,TRUE);
		if ( (has_index_html==TRUE) && (url[0]!='?') )					// if we have an index.html and we dont have a ? url 
		{
			os_printf("Has index.html\n");
			connections[h].ht_content_type=HT_CONTENT_TEXTHTML;
			connections[h].ht_state=HT_STATE_SENDING_FILE;				// then we send index.html
			os_sprintf(connections[h].ht_filename,"index.html");
			send_header (arg, connections[h].ht_content_type, -1, FALSE);
			return;
		}
		connections[h].ht_state=HT_STATE_SENDING_FILELIST;				// assume we mean to send an index (for now)
		connections[h].ht_content_type=HT_CONTENT_TEXTHTML;
		send_header (arg, connections[h].ht_content_type, -1, FALSE);
		return;
	}

	//os_printf("trying to match url\n");
	// Try and match URL with files stored in flash
	for (i=0;i<MFS_FILECOUNT;i++)								// For every file
	{
        	if ( MFSFileList(i+1, (char*)&st, &fsize )==0)
		{
			if (strcmp(st,url)==0)							// we have a match for a filename
			{
				//os_printf("Got match for file %s\n",url);
				connections[h].ht_state=HT_STATE_SENDING_FILE;
				connections[h].ht_count=0;
				strcpy(connections[h].ht_filename,url);
				u=strlen(connections[h].ht_filename);
				connections[h].ht_content_type=detect_content_type((char*)&connections[h].ht_filename);
				send_header (arg, connections[h].ht_content_type, -1, FALSE);
				//os_printf("Found a filename match\n");
				return;
			}
		}
	}
	
	// If we are still here then file not found, so 404
	connections[h].ht_state=HT_STATE_NOTFOUND;
	connections[h].ht_filename[0]=0;

#ifdef INDEX_INSTEAD_OF_NOTFOUND
os_printf("has_index_html=%d\n",has_index_html);
	if (has_index_html==TRUE) 
	{
		os_printf("Sending index.html instead of 404\n");
		connections[h].ht_state=HT_STATE_SENDING_FILE;	
		connections[h].ht_content_type=HT_CONTENT_TEXTHTML;
		connections[h].ht_content_type=detect_content_type((char*)&connections[h].ht_filename);
		os_sprintf(connections[h].ht_filename,"index.html");
		send_header (arg, connections[h].ht_content_type, -1, TRUE);			// Header will re-write url to /
		return;
	}
#endif
	send_header (arg, connections[h].ht_content_type, -1, FALSE);
    }
}



void ICACHE_FLASH_ATTR tcp_send_data ( void *arg )
{
    //struct espconn *xconn = (struct espconn *)arg;
    int h;
    char buf[1024];
    char st[65];
    int fsize=0;
    int sendsize=0;

    //os_printf("tcp_send_data, ht_state %d, ht_count %d\n",ht_state,ht_count);
    h=connectionhandle(arg, HANDLE_LOOKUP);								// Find our connection details
    if (h<0)
  	return;


    buf[0]=0;
    switch (connections[h].ht_state)
    {
    	case HT_STATE_CLOSE:
		espconn_disconnect(arg);
    		h=connectionhandle(arg, HANDLE_REMOVE);	
		return;
	break;

	case HT_STATE_SENDING_FILELIST:
		if (connections[h].ht_count<=MFS_FILECOUNT)						// if more files to list
		{
        		if ( MFSFileList(connections[h].ht_count+1, (char*)&st, &fsize )==0)
			{
				if (connections[h].ht_count==0)
                			os_sprintf(buf,"<pre><a href=%s>%d\t%d\t\t%s</a>\n",st,connections[h].ht_count+1,fsize,st);
                		else	os_sprintf(buf,"<a href=%s>%d\t%d\t\t%s</a>\n",st,connections[h].ht_count,fsize,st);
				sendsize=strlen(buf);
				connections[h].ht_count++;
			}
			else connections[h].ht_state=HT_STATE_CLOSE;					// next time round close the socket
		}
		else connections[h].ht_state=HT_STATE_CLOSE;
	break;

	case HT_STATE_SENDING_FILE:
		send_file_chunk(arg);									// Send more file
		connections[h].ht_count++;
	break; 

	case HT_STATE_NOTFOUND:
		os_sprintf(st,"<html><head>\n404 Not found</head>\n");
    		espconn_send ( arg, (uint8 *)st, strlen(st)  );	
		os_printf("404 - Not found\n");
		connections[h].ht_state=HT_STATE_CLOSE;
		return;
	break;
    }

    espconn_send ( arg, (uint8 *)&buf, sendsize );						// send next chunk of tcp data

    // It is perfectly valid to close the socket and force the browser to re-open it for the next request, tell Safari ***ts
    if (connections[h].ht_state==HT_STATE_CLOSE)
    {
	espconn_disconnect(arg);
    	h=connectionhandle(arg, HANDLE_REMOVE);	
    }
}


 
// This is a TCP error handler
void ICACHE_FLASH_ATTR tcp_reconnect_cb ( void *arg, sint8 err )
{
    os_printf ( "TCP reconnect (error)\n" );
}


void ICACHE_FLASH_ATTR tcp_disconnect_cb ( void *arg )
{
    int h;
    struct espconn *conn = (struct espconn *)arg;

    h=connectionhandle(conn, HANDLE_REMOVE);
}

 

void ICACHE_FLASH_ATTR tcp_connect_cb ( void *arg )
{
    struct espconn *conn = (struct espconn *)arg;
    int h;
    char buf[16];

    //os_printf ( "TCP connect\n" );
    //os_printf ( "  remote ip: %s\n", ip2str ( buf, conn->proto.tcp->remote_ip ) );
    //os_printf ( "  remote port: %d\n", conn->proto.tcp->remote_port );

    h=connectionhandle(conn, HANDLE_CREATE);
    if (h<0)
          os_printf("Error trying to create new handle\n");
    //else  os_printf("Got handle %d\n",h);

    espconn_regist_recvcb( conn, tcp_receive_data );
    espconn_regist_sentcb( conn, tcp_send_data );
}



static struct espconn server_conn;
static esp_tcp my_tcp_conn;

void ICACHE_FLASH_ATTR init_jahttp( void )
{
    register struct espconn *c = &server_conn;
    char *x;
    int i;
    char st[32];
    int fsize;

    c->type = ESPCONN_TCP;
    c->state = ESPCONN_NONE;
    my_tcp_conn.local_port=80;
    c->proto.tcp=&my_tcp_conn;

    espconn_regist_reconcb ( c, tcp_reconnect_cb);
    espconn_regist_connectcb ( c, tcp_connect_cb);
    espconn_regist_disconcb ( c, tcp_disconnect_cb);
    espconn_tcp_set_max_con(12);

    if ( espconn_accept(c) != ESPCONN_OK ) {
	os_printf("Error starting server %d\n", 0);
	return;
    }

    /* Interval in seconds to timeout inactive connections */
    espconn_regist_time(c, 20, 0);

    // x = (char *) os_zalloc ( 4 );
    // os_printf ( "Got mem: %08x\n", x );


    // See if the memory file system image contains index.html ?
    for (i=1;i<=MFS_FILECOUNT;i++)
    {
        if ( MFSFileList(i, (char*)&st, &fsize )==0)
	{
		os_printf("compare [%s][index.html]\n",st);
		if (strcmp(st,"index.html")==0)
			has_index_html=TRUE; 
	}
    }

    // Mark all handles unused
    for (i=0;i<MAX_CONNECTIONS;i++)
	connections[i].connected = FALSE; 

    os_printf ( "Server ready,\n");
    if (has_index_html==TRUE)
	os_printf("Using index.html from flash\n");
    os_printf("port %d\n\n",my_tcp_conn.local_port );


#ifdef DEBUG
    os_timer_setfn(&sh_timer,(os_timer_t *)sh_timer_cb,NULL);
    os_timer_arm(&sh_timer,100,1);
#endif

#ifdef LED_ACTIVITY
    GPIO_OUTPUT_SET(LED_ACTIVITY,0);			// low
    os_delay_us(2000);
    GPIO_OUTPUT_SET(LED_ACTIVITY,1);			// gpio high
    os_delay_us(1000);
    os_timer_setfn(&led_off_timer,(os_timer_t *)led_off_timer_cb,NULL);
    os_timer_arm(&led_off_timer,500,1);
#endif
}



// Maybe bring this back one day
//void
//wifi_event ( System_Event_t *e )
//{
    //int event = e->event;

    //if ( event == EVENT_STAMODE_GOT_IP ) {
	//os_printf ( "WIFI Event, got IP\n" );
	//show_ip ();
	//setup_server ();
    //} else if ( event == EVENT_STAMODE_CONNECTED ) {
	//os_printf ( "WIFI Event, connected\n" );
    //} else if ( event == EVENT_STAMODE_DISCONNECTED ) {
	//os_printf ( "WIFI Event, disconnected\n" );
    //} else {
	//os_printf ( "Unknown event %d !\n", event );
    //}
//}
 

