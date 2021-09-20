/*******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jean-David Gadina - www.xs-labs.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <poll.h>

bool exec( const std::string & command, const std::vector< std::string > & args, int64_t & processID, std::string & output, std::string & error, bool wait );

int main( void )
{
    int64_t     pid = 0;
    std::string out = {};
    std::string err = {};
    
    exec( "/bin/ls", { "-al", "/" }, pid, out, err, true );
    
    std::cout << "PID:    " << pid << std::endl;
    std::cout << "Output: " << out << std::endl;
    std::cout << "Error:  " << err << std::endl;
    
    return 0;
}

namespace Core
{
    class Defer
    {
        public:
            
            ~Defer()
            {
                for( const auto & f: this->_f )
                {
                    f();
                }
            }
            
            Defer & operator += ( const std::function< void( void ) > & f )
            {
                this->_f.push_back( f );
                
                return *( this );
            }
            
        private:
            
            std::vector< std::function< void( void ) > > _f;
    };
}

bool exec( const std::string & command, const std::vector< std::string > & args, int64_t & processID, std::string & output, std::string & error, bool wait )
{
    Core::Defer                defer;
    int                        pipeOut[ 2 ];
    int                        pipeErr[ 2 ];
    posix_spawn_file_actions_t actions;
    
    processID = 0;
    
    if( pipe( pipeOut ) != 0 || pipe( pipeErr ) != 0 )
    {
        return false;
    }
    
    if( posix_spawn_file_actions_init( &actions ) != 0 )
    {
        return false;
    }
    
    defer += [ & ]
    {
        posix_spawn_file_actions_destroy( &actions );
    };
    
    if( posix_spawn_file_actions_addclose( &actions, pipeOut[ 0 ] ) != 0 )
    {
        return false;
    }
    
    if( posix_spawn_file_actions_addclose( &actions, pipeErr[ 0 ] ) != 0 )
    {
        return false;
    }
    
    if( posix_spawn_file_actions_adddup2( &actions, pipeOut[ 1 ], 1 ) != 0 )
    {
        return false;
    }
    
    if( posix_spawn_file_actions_adddup2( &actions, pipeErr[ 1 ], 2 ) != 0 )
    {
        return false;
    }
    
    if( posix_spawn_file_actions_addclose( &actions, pipeOut[ 1 ] ) != 0 )
    {
        return false;
    }
    
    if( posix_spawn_file_actions_addclose( &actions, pipeErr[ 1 ] ) != 0 )
    {
        return false;
    }
    
    char ** argv = static_cast< char ** >( calloc( sizeof( char * ), args.size() + 2 ) );
    char ** envp = { nullptr };
    
    if( argv == nullptr )
    {
        return false;
    }
    
    defer += [ & ]
    {
        for( size_t i = 0; i < args.size() + 2; i++ )
        {
            free( argv[ i ] );
        }
        
        free( argv );
    };
    
    argv[ 0 ]               = strdup( command.c_str() );
    argv[ args.size() + 1 ] = nullptr;
    
    for( size_t i = 1; i < args.size() + 1; i++ )
    {
        argv[ i ] = strdup( args[ i - 1 ].c_str() );
    }
    
    pid_t pid    = 0;
    int   status = posix_spawnp( &pid, command.c_str(), &actions, nullptr, argv, envp );
    processID    = pid;
    
    close( pipeOut[ 1 ] );
    close( pipeErr[ 1 ] );
    
    if( status != 0 )
    {
        return false;
    }
    
    if( wait )
    {
        char    buff[ 1024 ];
        ssize_t n[ 2 ]   = { -1, -1 };
        pollfd  fds[ 2 ] = { { pipeOut[ 0 ], POLLIN, 0 }, { pipeErr[ 0 ], POLLIN, 0 } };
        
        for( int rval; ( rval = poll( fds, 2, -1) ) > 0; )
        {
            if( n[ 0 ] != 0 && fds[ 0 ].revents & POLLIN )
            {
                n[ 0 ] = read( pipeOut[ 0 ], buff, sizeof( buff ) );
                
                if( n[ 0 ] > 0 )
                {
                    output += std::string( buff, static_cast< size_t >( n[ 0 ] ) );
                }
            }
            else if( n[ 1 ] != 0 && fds[ 1 ].revents & POLLIN )
            {
                n[ 1 ] = read( pipeErr[ 0 ], buff, sizeof( buff ) );
                
                if( n[ 1 ] > 0 )
                {
                    error += std::string( buff, static_cast< size_t >( n[ 1 ] ) );
                }
            }
            else
            {
                break;
            }
        }
        
        waitpid( pid, &status, 0 );
    }
    
    return true;
}
