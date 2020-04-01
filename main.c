#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <inttypes.h>
#include <windows.h>
#include <tlhelp32.h>

#define APP_IMPLEMENTATION
#define APP_WINDOWS
#include "app.h"

#define HISTORY 5

typedef struct _CPU_MEASURE {
	int count;
	struct {
        BOOL active;
		DWORD processID;
		DWORD parentProcessID;
        ULONG64 cpuTime;
        ULONG64 prevCpuTime;
        ULONG64 cycleTime;
        ULONG64 prevCycleTime;
	} items[ 256 ];
    ULONG64 cpuTime[ HISTORY ];
    ULONG64 cycleTime[ HISTORY ];
    ULONG64 medCpuTime;
    ULONG64 medCycleTime;
    double cpuRatio;
    double prevCpuRatio;
    double cycleRatio;
    double prevCycleRatio;
} CPU_MEASURE;


void GetSymphonyProcessList( CPU_MEASURE* list, char const* processName ) {
	HANDLE snapShot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if( snapShot == INVALID_HANDLE_VALUE ) {
		return;
	}

	PROCESSENTRY32 procentry = { sizeof( PROCESSENTRY32 ) };
	BOOL valid = Process32First( snapShot, &procentry );
	while( valid ) {
		// Only add symphony processes to the list
		if( strstr( procentry.szExeFile, processName ) ) {
            DWORD processID = procentry.th32ProcessID;
            HANDLE hproc = OpenProcess( PROCESS_QUERY_INFORMATION, FALSE, processID );

            FILETIME creationTime;
            FILETIME exitTime;
            FILETIME kernelTime;
            FILETIME userTime;
            GetProcessTimes( hproc, &creationTime, &exitTime, &kernelTime, &userTime );

            ULONG64 cycleTime;
            QueryProcessCycleTime( hproc, &cycleTime );

            CloseHandle( hproc );

            int index = -1;
            for( int i = 0; i < list->count; ++i ) {
                if( list->items[ i ].processID == processID ) {
                    index = i;
                    break;
                }
            }

            if( index < 0 ) {
		        if( list->count >= sizeof( list->items ) / sizeof( *list->items ) ) {
			        break;
		        }
                index = list->count++;
            }

            ULONG64 cpuTime = 0;
            cpuTime += ( ( (ULONG64) kernelTime.dwHighDateTime ) << 32 ) + kernelTime.dwLowDateTime;
            cpuTime += ( ( (ULONG64) userTime.dwHighDateTime ) << 32 ) + userTime.dwLowDateTime;

            list->items[ index ].active = TRUE;
			list->items[ index ].processID = processID;
			list->items[ index ].parentProcessID = procentry.th32ParentProcessID;
			list->items[ index ].cpuTime = cpuTime;
            list->items[ index ].cycleTime = cycleTime;
		}

		procentry.dwSize = sizeof( PROCESSENTRY32 );
		valid = Process32Next( snapShot, &procentry );
	}

	CloseHandle( snapShot );
}


int ulongcmp( void* usr, void const* a, void const* b ) {
    (void) usr;
    if( *(ULONG64*)a < *(ULONG64*)b ) return -1;
    if( *(ULONG64*)a > *(ULONG64*)b ) return 1;
    return 0;
}


void CaptureCpu( CPU_MEASURE* data, char const* processName ) {
	// List all processes
	for( int i = 0; i < data->count; ++i ) {
        data->items[ i ].active = FALSE;
    }
	GetSymphonyProcessList( data, processName );

	// Calculate total CPU times
    ULONG64 cpuTime = 0;
    ULONG64 cycleTime = 0;
	for( int i = 0; i < data->count; ++i ) {
        if( data->items[ i ].active ) {
            ULONG64 cpuDelta = data->items[ i ].cpuTime - data->items[ i ].prevCpuTime;
            ULONG64 cycleDelta = data->items[ i ].cycleTime - data->items[ i ].prevCycleTime;
            if( data->items[ i ].prevCpuTime > 0 && data->items[ i ].prevCycleTime > 0 ) {
                cpuTime += cpuDelta;
                cycleTime += cycleDelta;
            }
            data->items[ i ].prevCpuTime = data->items[ i ].cpuTime;
            data->items[ i ].prevCycleTime = data->items[ i ].cycleTime;
        }
	}
	//printf( "CpuTime: %d  CycleTime: %d\n", (int) cpuTime, (int) cycleTime );
    
    memmove( data->cpuTime, data->cpuTime + 1, sizeof( data->cpuTime ) - sizeof( *data->cpuTime ) );
    data->cpuTime[ HISTORY - 1] = cpuTime;
    ULONG64 cpuTimes[ HISTORY ];
    memcpy( cpuTimes, data->cpuTime, sizeof( data->cpuTime ) );
    qsort_s( cpuTimes, HISTORY, sizeof( ULONG64 ), ulongcmp, NULL );
    data->medCpuTime = cpuTimes[ HISTORY / 2 ];
    
    memmove( data->cycleTime, data->cycleTime + 1, sizeof( data->cycleTime ) - sizeof( *data->cycleTime ) );
    data->cycleTime[ HISTORY - 1] = cycleTime;
    ULONG64 cycleTimes[ HISTORY ];
    memcpy( cycleTimes, data->cycleTime, sizeof( data->cycleTime ) );
    qsort_s( cycleTimes, HISTORY, sizeof( ULONG64 ), ulongcmp, NULL );
    data->medCycleTime = cycleTimes[ HISTORY / 2 ];

    data->prevCpuRatio = data->cpuRatio;
    data->prevCycleRatio = data->cycleRatio;
    data->cpuRatio = ( (double) data->medCpuTime ) / 30000000.0;
    data->cycleRatio = ( (double) data->medCycleTime ) / 10000000000.0;

}


void line( APP_U32* canvas, int width, int height, int x1, int y1, int x2, int y2, APP_U32 color ) {
	int dx = x2 - x1;
	dx = dx < 0 ? -dx : dx;
	int sx = x1 < x2 ? 1 : -1;
	int dy = y2 - y1;
	dy = dy < 0 ? -dy : dy;
	int sy = y1 < y2 ? 1 : -1; 
	int err = ( dx > dy ? dx : -dy ) / 2;
	 
	int x = x1;
	int y = y1;
	while( x != x2 || y != y2 ) {
        if( x >= 0 && x < width && y >= 0 && y < height ) {
            canvas[ x + y * width ] = color;
        }
		
		int e2 = err;
		if( e2 > -dx ) { 
            err -= dy; 
            x += sx; 
        }
		if( e2 < dy ) { 
            err += dx; 
            y += sy; 
        }
	}

    if( x >= 0 && x < width && y >= 0 && y < height ) {
        canvas[ x + y * width ] = color;
    }
}


void drawCycle( CPU_MEASURE* data, APP_U32 color, int pos, APP_U32* canvas, int width, int height ) {
    int x = pos;
    int y = height - (int)( data->cycleRatio * height ) - 1;
    int prevY = height - (int)( data->prevCycleRatio * height ) - 1;
    line( canvas, width, height, x - 1, prevY, x, y, color );
}


void drawCpu( CPU_MEASURE* data, APP_U32 color, int pos, APP_U32* canvas, int width, int height ) {
    int x = pos;
    int y = height - (int)( data->cpuRatio * height ) - 1;
    int prevY = height - (int)( data->prevCpuRatio * height ) - 1;
    line( canvas, width, height, x - 1, prevY, x, y, color );
}


void writeToFile( FILE* fp, ULONG64 time, CPU_MEASURE* symphony, CPU_MEASURE* chrome ) {
    fprintf( fp, "%" PRIu64 ", %" PRIu64 ", %" PRIu64 ", \n", time, symphony->cpuTime[ HISTORY -1 ], chrome->cpuTime[ HISTORY -1 ] );
}


int app_proc( app_t* app, void* user_data ) {
    char const* filename = (char const*) user_data;

    int const width = 400;
    int const height = 200;
    APP_U32* canvas = malloc( width * height * sizeof( APP_U32 ) );
    memset( canvas, 0xff, width * height * sizeof( APP_U32 ) );
    app_screenmode( app, APP_SCREENMODE_WINDOW );
    app_window_size( app, width + width / 3, height + height / 3 );
    app_title( app, "CPU Usage - Symphony [Blue] - Chrome [Red]" );

	CPU_MEASURE symphony = { 0 };
    CPU_MEASURE chrome = { 0 };

    FILE* fp = NULL;
    if( filename ) {
        fp = fopen( filename, "w" );
    }

    LARGE_INTEGER s;
    QueryPerformanceCounter( &s );
    ULONG64 start = s.QuadPart;

    CaptureCpu( &symphony, "Symphony.exe" );
    CaptureCpu( &chrome, "chrome.exe" );

    int pos = 0;
    while( app_yield( app ) != APP_STATE_EXIT_REQUESTED ) {
        Sleep( 1000 );

        LARGE_INTEGER c;
        QueryPerformanceCounter( &c );
        ULONG64 time = c.QuadPart - start;

        CaptureCpu( &symphony, "Symphony.exe" );
        CaptureCpu( &chrome, "chrome.exe" );

        if( fp ) {
            writeToFile( fp, time, &symphony, &chrome );
        }

        drawCpu( &symphony, 0xffff0000, pos, canvas, width, height );
        drawCpu( &chrome, 0xff0000ff, pos, canvas, width, height );

        ++pos;
        if( pos == width ) {
            int scroll = width / 8;
            pos -= scroll;
            for( int i = 0; i < height; ++i ) {
                memmove( canvas + width * i, canvas + width * i + scroll, ( width - scroll ) * sizeof( APP_U32 ) );
                memset( canvas + width * i + ( width - scroll ), 0xff, scroll * sizeof( APP_U32 ) );
            }
        }

        app_present( app, canvas, width, height, 0xffffff, 0xffffff );
    }

    if( fp ) {
        fclose( fp );
        fp = NULL;
    }

    free( canvas );
    return EXIT_SUCCESS;
}


int noGraphMode( char const* filename, int duration ) {
	CPU_MEASURE symphony = { 0 };
    CPU_MEASURE chrome = { 0 };

    FILE* fp = fopen( filename, "w" );

    LARGE_INTEGER s;
    QueryPerformanceCounter( &s );
    ULONG64 start = s.QuadPart;

    CaptureCpu( &symphony, "Symphony.exe" );
    CaptureCpu( &chrome, "chrome.exe" );

    for( int i = 0; i < duration; ++i ) {
        Sleep( 1000 );

        LARGE_INTEGER c;
        QueryPerformanceCounter( &c );
        ULONG64 time = c.QuadPart - start;

        CaptureCpu( &symphony, "Symphony.exe" );
        CaptureCpu( &chrome, "chrome.exe" );

        writeToFile( fp, time, &symphony, &chrome );
    }

    fclose( fp );
    return EXIT_SUCCESS;    
}


int main( int argc, char** argv ) {
    if( ( argc > 1 && strcmp( argv[ 1 ], "--graph" ) == 0 ) || argc == 1 ) {
        char* filename = argc > 2 ? argv[ 2 ] : NULL;
        return app_run( app_proc, filename, NULL, NULL, NULL );
    } else if( argc == 3 ) {
        char const* filename = argv[ 1 ]; 
        int duration = atoi( argv[ 2 ] );
        return noGraphMode( filename, duration );
    }

    return EXIT_FAILURE;
}
