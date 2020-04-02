#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <inttypes.h>
#include <windows.h>
#include <tlhelp32.h>

#define APP_IMPLEMENTATION
#define APP_WINDOWS
#include "app.h"

#define HISTORY 5
#define MAX_PROCESSES 32


struct cpu_data_t {
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
};


void get_process_data( struct cpu_data_t* list, char const* process_name ) {
	HANDLE snapShot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if( snapShot == INVALID_HANDLE_VALUE ) {
		return;
	}

	PROCESSENTRY32 procentry = { sizeof( PROCESSENTRY32 ) };
	BOOL valid = Process32First( snapShot, &procentry );
	while( valid ) {
		// Only add symphony processes to the list
		if( strstr( procentry.szExeFile, process_name ) ) {
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


void get_cpu_data( struct cpu_data_t* data, char const* process_name ) {
	// List all processes
	for( int i = 0; i < data->count; ++i ) {
        data->items[ i ].active = FALSE;
    }
	get_process_data( data, process_name );

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


void draw_cycle( struct cpu_data_t* data, APP_U32 color, int pos, APP_U32* canvas, int width, int height ) {
    int x = pos;
    int y = height - (int)( data->cycleRatio * height ) - 1;
    int prevY = height - (int)( data->prevCycleRatio * height ) - 1;
    line( canvas, width, height, x - 1, prevY, x, y, color );
}


void draw_cpu( struct cpu_data_t* data, APP_U32 color, int pos, APP_U32* canvas, int width, int height ) {
    int x = pos;
    int y = height - (int)( data->cpuRatio * height ) - 1;
    int prevY = height - (int)( data->prevCpuRatio * height ) - 1;
    line( canvas, width, height, x - 1, prevY, x, y, color );
}


enum arg_type_t {
    ARG_TYPE_NONE,
    ARG_TYPE_INVALID,
    ARG_TYPE_STRING,
    ARG_TYPE_PROCESS,
    ARG_TYPE_DURATION,
    ARG_TYPE_FILE,
    ARG_TYPE_GRAPH,
};


enum arg_type_t get_arg_type( int argc, char** argv, int index ) {
    if( index >= argc ) {
        return ARG_TYPE_NONE;
    }

    char const* a = argv[ index ];
    if( _stricmp( a, "-p" ) == 0 || _stricmp( a, "--process" ) == 0 ) {
        return ARG_TYPE_PROCESS;
    } else if( _stricmp( a, "-d" ) == 0 || _stricmp( a, "--duration" ) == 0 ) {
        return ARG_TYPE_DURATION;
    } else if( _stricmp( a, "-f" ) == 0 || _stricmp( a, "--file" ) == 0 ) {
        return ARG_TYPE_FILE;
    } else if( _stricmp( a, "-g" ) == 0 || _stricmp( a, "--graph" ) == 0 ) {
        return ARG_TYPE_GRAPH;
    } else if( a[ 0 ] == '-' ) {
        return ARG_TYPE_INVALID;
    } else { 
        return ARG_TYPE_STRING;
    }
}


struct args_t {
    BOOL graph;
    int duration;
    FILE* fp;

    char const* process_names[ MAX_PROCESSES ];
    int process_count;
};


int parse_args( int argc, char** argv, struct args_t* args ) {
    memset( args, 0, sizeof( *args ) );
    int index = 1;
    enum arg_type_t type = get_arg_type( argc, argv, index++ );
    while( type != ARG_TYPE_NONE ) {
        if( type == ARG_TYPE_STRING ) {
            return EXIT_FAILURE;
        } else if( type == ARG_TYPE_PROCESS ) {
            if( get_arg_type( argc, argv, index ) != ARG_TYPE_STRING ) {
                printf( "-p or --process must be followed by a process name\n" );
                return EXIT_FAILURE;
            }
            if( args->process_count >= MAX_PROCESSES ) {
                printf( "only a maximum of %d processes are supported\n", MAX_PROCESSES );
            }
            args->process_names[ args->process_count++ ] = argv[ index++ ];
        } else if( type == ARG_TYPE_DURATION ) {
            if( get_arg_type( argc, argv, index ) != ARG_TYPE_STRING ) {
                printf( "-d or --duration must be followed by number of seconds\n" );
                return EXIT_FAILURE;
            }
            int duration = atoi( argv[ index++ ] );
            if( duration <= 0 ) {
                printf( "-d or --duration must be followed by number of seconds greater than 0\n" );
                return EXIT_FAILURE;
            }
            args->duration = duration;
        } else if( type == ARG_TYPE_FILE ) {
            if( get_arg_type( argc, argv, index ) != ARG_TYPE_STRING ) {
                printf( "-f or --file must be followed by a filename\n" );
                return EXIT_FAILURE;
            }
            char const* filename = argv[ index++ ];
            args->fp = fopen( filename, "w" );
            if( !args->fp ) {
                printf( "-f or --file must be followed by a valid filename\n" );
                printf( "  failed to open file: %s\n", filename );
                return EXIT_FAILURE;
            }
        } else if( type == ARG_TYPE_GRAPH ) {
            if( get_arg_type( argc, argv, index ) == ARG_TYPE_STRING ) {
                printf( "-g or --graph should not be followed by a parameter\n" );
                return EXIT_FAILURE;
            }
            args->graph = TRUE;
        }

        type = get_arg_type( argc, argv, index++ );
    }
    
    if( !args->graph && args->duration == 0 ) {
        printf( "When running without --graph or -g, a duration must be specified with --duration or -d\n" );
        return EXIT_FAILURE;
    }

    if( !args->graph && !args->fp) {
        printf( "When running without --graph or -g, a valid filename must be specified with --file or -f\n" );
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


struct run_data_t {
    struct args_t* args;
    struct cpu_data_t cpu_data[ MAX_PROCESSES ];
    ULONG64 freq;
    ULONG64 start_time;
};


void init_run_data( struct run_data_t* data, struct args_t* args ) {
    memset( data, 0, sizeof( *data ) );
    data->args = args;

    LARGE_INTEGER f;
    QueryPerformanceFrequency( &f );
    data->freq = f.QuadPart;

    LARGE_INTEGER s;
    QueryPerformanceCounter( &s );
    data->start_time = s.QuadPart;

    for( int i = 0; i < args->process_count; ++i ) {        
        get_cpu_data( &data->cpu_data[ i ], args->process_names[ i ] );

        if( data->args->fp ) {
            fprintf( data->args->fp, "time, " );
            for( int i = 0; i < data->args->process_count; ++i ) {                
                fprintf( data->args->fp, "%s, ", data->args->process_names[ i ] );
            }

            fprintf( data->args->fp, "\n" );
            fflush( data->args->fp );
        }
    }
}


void run_step( struct run_data_t* data ) {
    Sleep( 1000 );

    LARGE_INTEGER c;
    QueryPerformanceCounter( &c );
    ULONG64 time = c.QuadPart - data->start_time;

    for( int i = 0; i < data->args->process_count; ++i ) {        
        get_cpu_data( &data->cpu_data[ i ], data->args->process_names[ i ] );
    }

    if( data->args->fp ) {
        fprintf( data->args->fp, "%" PRIu64 ", ", time );
        for( int i = 0; i < data->args->process_count; ++i ) {                
            fprintf( data->args->fp, "%" PRIu64 ", ", data->cpu_data[ i ].cpuTime[ HISTORY -1 ] );
        }

        fprintf( data->args->fp, "\n" );
        fflush( data->args->fp );
    }
}


int app_proc( app_t* app, void* user_data ) {
    struct run_data_t* data = (struct run_data_t*) user_data;

    int const width = 400;
    int const height = 200;
    APP_U32* canvas = malloc( width * height * sizeof( APP_U32 ) );
    memset( canvas, 0xff, width * height * sizeof( APP_U32 ) );
    app_screenmode( app, APP_SCREENMODE_WINDOW );
    app_window_size( app, width + width / 3, height + height / 3 );
    
    int count = 0;
    char title[ 1024 ] = "CPU Measure";
    if( data->args->process_count == 1 ) {
        snprintf( title, sizeof( title ), "CPU Measure - %s", 
            data->args->process_names[ 0 ] );
        count = 1;
    } else if( data->args->process_count == 2 ) {
        snprintf( title, sizeof( title ), "CPU Measure - %s [Blue] - %s [Red]", 
            data->args->process_names[ 0 ], 
            data->args->process_names[ 1 ] );
        count = 2;
    } else if( data->args->process_count >= 3 ) {
        snprintf( title, sizeof( title ), "CPU Measure - %s [Blue] - %s [Red] - %s [Green]", 
            data->args->process_names[ 0 ], 
            data->args->process_names[ 1 ], 
            data->args->process_names[ 2 ] );
        count = 3;
    }        
    app_title( app, title );

    APP_U32 colors[ 3 ] = { 0xffff0000, 0xff0000ff, 0xff00ff00 };

    int pos = 0;
    int steps = 0;
    while( app_yield( app ) != APP_STATE_EXIT_REQUESTED ) {
        run_step( data );
        if( data->args->duration && ++steps >= data->args->duration ) {
            break;
        }

        for( int i = 0; i < count; ++i ) {
            draw_cpu( &data->cpu_data[ i ], colors[ i ], pos, canvas, width, height );
        }

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

    free( canvas );
    return EXIT_SUCCESS;
}


int run_no_graph( struct run_data_t* data ) {
    for( int i = 0; i < data->args->duration; ++i ) {
        run_step( data );
    }

    return EXIT_SUCCESS;    
}


int main( int argc, char** argv ) {
    struct args_t args = { 0 };
    
    int result = EXIT_SUCCESS;
    
    if( argc <= 1 ) {
        // defaults when no args provided
        args.graph = TRUE;
        args.duration = 0;
    } else {
        // parse args
        result = parse_args( argc, argv, &args );
    }

    // default process names if none given
    if( args.process_count == 0 ) {
        args.process_count = 2;
        args.process_names[ 0 ] = "Symphony.exe";
        args.process_names[ 1 ] = "chrome.exe";
    }

    if( result == EXIT_FAILURE ) {
        printf( "USAGE\n" );
        // TODO: print usage
        return EXIT_FAILURE;
    }

    struct run_data_t data;
    init_run_data( &data, &args );

    if( args.graph ) {
        result = app_run( app_proc, &data, NULL, NULL, NULL );
    } else {
        result = run_no_graph( &data );
    }

    if( args.fp ) {
        fclose( args.fp );
    }

    return result;
}
