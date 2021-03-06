// locks.h

/*
 *    Copyright (C) 2010 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "mutex.h"

#if BOOST_VERSION >= 103500
  #define BOOST_RWLOCK
#else

  #if defined(_WIN32)
    #error need boost >= 1.35 for windows
  #endif
 
  #include <pthread.h>

#endif

#ifdef BOOST_RWLOCK
#include <boost/thread/shared_mutex.hpp>
#undef assert
#define assert MONGO_assert
#endif

namespace mongo {

#ifdef BOOST_RWLOCK
    class RWLock {
        boost::shared_mutex _m;
    public:
#if defined(_DEBUG)
        const char *_name;
        RWLock(const char *name) : _name(name) { }
#else
        RWLock(const char *) { }
#endif
        void lock(){
            _m.lock();
#if defined(_DEBUG)
            mutexDebugger.entering(_name);
#endif
        }
        void unlock(){
#if defined(_DEBUG)
            mutexDebugger.leaving(_name);
#endif
            _m.unlock();
        }
        
        void lock_shared(){
#if defined(_DEBUG)
            mutexDebugger.entering(_name);
#endif
            _m.lock_shared();
        }
        
        void unlock_shared(){
#if defined(_DEBUG)
            mutexDebugger.leaving(_name);
#endif
            _m.unlock_shared();
        }

        bool lock_shared_try( int millis ){
            boost::system_time until = get_system_time();
            until += boost::posix_time::milliseconds(millis);
            return _m.timed_lock_shared( until );
        }

        bool lock_try( int millis ){
            boost::system_time until = get_system_time();
            until += boost::posix_time::milliseconds(millis);
            return _m.timed_lock( until );
        }


    };
#else
    class RWLock {
        pthread_rwlock_t _lock;

        inline void check( int x ){
            if( x == 0 )
                return;
            log() << "pthread rwlock failed: " << x << endl;
            assert( x == 0 );
        }

    public:
#if defined(_DEBUG)
        const char *_name;
        RWLock(const char *name) : _name(name) {
#else
        RWLock(const char *) {
#endif
            check( pthread_rwlock_init( &_lock , 0 ) );
        }
        
        ~RWLock(){
            if ( ! __destroyingStatics ){
                check( pthread_rwlock_destroy( &_lock ) );
            }
        }

        void lock(){
            check( pthread_rwlock_wrlock( &_lock ) );
#if defined(_DEBUG)
            mutexDebugger.entering(_name);
#endif
        }
        void unlock(){
#if defined(_DEBUG)
            mutexDebugger.leaving(_name);
#endif
            check( pthread_rwlock_unlock( &_lock ) );
        }
        
        void lock_shared(){
            check( pthread_rwlock_rdlock( &_lock ) );
#if defined(_DEBUG)
            mutexDebugger.entering(_name);
#endif
        }
        
        void unlock_shared(){
#if defined(_DEBUG)
            mutexDebugger.leaving(_name);
#endif
            check( pthread_rwlock_unlock( &_lock ) );
        }
        
        bool lock_shared_try( int millis ){
            return _try( millis , false );
        }

        bool lock_try( int millis ){
            return _try( millis , true );
        }

        bool _try( int millis , bool write ){
            while ( true ) {
                int x = write ? 
                    pthread_rwlock_trywrlock( &_lock ) : 
                    pthread_rwlock_tryrdlock( &_lock );
                
                if ( x <= 0 )
                    return true;
                
                if ( millis-- <= 0 )
                    return false;
                
                if ( x == EBUSY ){
                    sleepmillis(1);
                    continue;
                }
                check(x);
            } 
            
            return false;
        }

    };
    

#endif

    struct rwlock {
        rwlock( const RWLock& lock , bool write , bool alreadyHaveLock = false )
            : _lock( (RWLock&)lock ) , _write( write ){

            if ( ! alreadyHaveLock ){
                if ( _write )
                    _lock.lock();
                else
                    _lock.lock_shared();
            }
        }

        ~rwlock(){
            if ( _write )
                _lock.unlock();
            else
                _lock.unlock_shared();
        }
        
        RWLock& _lock;
        bool _write;
    };
}
