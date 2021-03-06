/* queryoptimizer.h */

/**
*    Copyright (C) 2008 10gen Inc.
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

#include "cursor.h"
#include "jsobj.h"
#include "queryutil.h"
#include "matcher.h"

namespace mongo {
    
    class IndexDetails;
    class IndexType;

    class QueryPlan : boost::noncopyable {
    public:
        QueryPlan(NamespaceDetails *_d, 
                  int _idxNo, // -1 = no index
                  const FieldRangeSet &fbs,
                  const BSONObj &order,
                  const BSONObj &startKey = BSONObj(),
                  const BSONObj &endKey = BSONObj() ,
                  string special="" );

        /* If true, no other index can do better. */
        bool optimal() const { return optimal_; }
        /* ScanAndOrder processing will be required if true */
        bool scanAndOrderRequired() const { return scanAndOrderRequired_; }
        /* When true, the index we are using has keys such that it can completely resolve the
         query expression to match by itself without ever checking the main object.
         */
        bool exactKeyMatch() const { return exactKeyMatch_; }
        /* If true, the startKey and endKey are unhelpful and the index order doesn't match the 
           requested sort order */
        bool unhelpful() const { return unhelpful_; }
        int direction() const { return direction_; }
        shared_ptr<Cursor> newCursor( const DiskLoc &startLoc = DiskLoc() , int numWanted=0 ) const;
        shared_ptr<Cursor> newReverseCursor() const;
        BSONObj indexKey() const;
        const char *ns() const { return fbs_.ns(); }
        NamespaceDetails *nsd() const { return d; }
        BSONObj query() const { return fbs_.query(); }
        BSONObj simplifiedQuery( const BSONObj& fields = BSONObj() ) const { return fbs_.simplifiedQuery( fields ); }
        const FieldRange &range( const char *fieldName ) const { return fbs_.range( fieldName ); }
        void registerSelf( long long nScanned ) const;
        // just for testing
        BoundList indexBounds() const { return indexBounds_; }
    private:
        NamespaceDetails *d;
        int idxNo;
        const FieldRangeSet &fbs_;
        const BSONObj &order_;
        const IndexDetails *index_;
        bool optimal_;
        bool scanAndOrderRequired_;
        bool exactKeyMatch_;
        int direction_;
        BoundList indexBounds_;
        bool endKeyInclusive_;
        bool unhelpful_;
        string _special;
        IndexType * _type;
    };

    // Inherit from this interface to implement a new query operation.
    // The query optimizer will clone the QueryOp that is provided, giving
    // each clone its own query plan.
    class QueryOp {
    public:
        QueryOp() : _complete(), _stopRequested(), _qp(), _error() {}
        virtual ~QueryOp() {}
        
        /** this gets called after a query plan is set? ERH 2/16/10 */
        virtual void init() = 0;
        virtual void next() = 0;
        virtual bool mayRecordPlan() const = 0;
        
        /** @return a copy of the inheriting class, which will be run with its own
                    query plan.  If multiple plan sets are required for an $or query,
                    the QueryOp of the winning plan from a given set will be cloned
                    to generate QueryOps for the subsequent plan set.
        */
        virtual QueryOp *clone() const = 0;
        bool complete() const { return _complete; }
        bool error() const { return _error; }
        bool stopRequested() const { return _stopRequested; }
        string exceptionMessage() const { return _exceptionMessage; }
        const QueryPlan &qp() const { return *_qp; }
        // To be called by QueryPlanSet::Runner only.
        void setQueryPlan( const QueryPlan *qp ) { _qp = qp; }
        void setExceptionMessage( const string &exceptionMessage ) {
            _error = true;
            _exceptionMessage = exceptionMessage;
        }
    protected:
        void setComplete() { _complete = true; }
        void setStop() { setComplete(); _stopRequested = true; }
    private:
        bool _complete;
        bool _stopRequested;
        string _exceptionMessage;
        const QueryPlan *_qp;
        bool _error;
    };
    
    // Set of candidate query plans for a particular query.  Used for running
    // a QueryOp on these plans.
    class QueryPlanSet {
    public:

        typedef boost::shared_ptr< QueryPlan > PlanPtr;
        typedef vector< PlanPtr > PlanSet;

        QueryPlanSet( const char *ns,
                     const BSONObj &query,
                     const BSONObj &order,
                     const BSONElement *hint = 0,
                     bool honorRecordedPlan = true,
                     const BSONObj &min = BSONObj(),
                     const BSONObj &max = BSONObj() );
        int nPlans() const { return plans_.size(); }
        shared_ptr< QueryOp > runOp( QueryOp &op );
        template< class T >
        shared_ptr< T > runOp( T &op ) {
            return dynamic_pointer_cast< T >( runOp( static_cast< QueryOp& >( op ) ) );
        }
        BSONObj explain() const;
        bool usingPrerecordedPlan() const { return usingPrerecordedPlan_; }
        PlanPtr getBestGuess() const;
        void setBestGuessOnly() { _bestGuessOnly = true; }
        //for testing
        const FieldRangeSet &fbs() const { return fbs_; }
    private:
        void addOtherPlans( bool checkFirst );
        void addPlan( PlanPtr plan, bool checkFirst ) {
            if ( checkFirst && plan->indexKey().woCompare( plans_[ 0 ]->indexKey() ) == 0 )
                return;
            plans_.push_back( plan );
        }
        void init();
        void addHint( IndexDetails &id );
        struct Runner {
            Runner( QueryPlanSet &plans, QueryOp &op );
            shared_ptr< QueryOp > run();
            QueryOp &op_;
            QueryPlanSet &plans_;
            static void initOp( QueryOp &op );
            static void nextOp( QueryOp &op );
        };
        const char *ns;
        BSONObj query_;
        FieldRangeSet fbs_;
        PlanSet plans_;
        bool mayRecordPlan_;
        bool usingPrerecordedPlan_;
        BSONObj hint_;
        BSONObj order_;
        long long oldNScanned_;
        bool honorRecordedPlan_;
        BSONObj min_;
        BSONObj max_;
        string _special;
        bool _bestGuessOnly;
    };

    // Handles $or type queries by generating a QueryPlanSet for each $or clause
    // NOTE on our $or implementation: In our current qo implementation we don't
    // keep statistics on our data, but we can conceptualize the problem of
    // selecting an index when statistics exist for all index ranges.  The
    // d-hitting set problem on k sets and n elements can be reduced to the
    // problem of index selection on k $or clauses and n index ranges (where
    // d is the max number of indexes, and the number of ranges n is unbounded).
    // In light of the fact that d-hitting set is np complete, and we don't even
    // track statistics (so cost calculations are expensive) our first
    // implementation uses the following greedy approach: We take one $or clause
    // at a time and treat each as a separate query for index selection purposes.
    // But if an index range is scanned for a particular $or clause, we eliminate
    // that range from all subsequent clauses.  One could imagine an opposite
    // implementation where we select indexes based on the union of index ranges
    // for all $or clauses, but this can have much poorer worst case behavior.
    // (An index range that suits one $or clause may not suit another, and this
    // is worse than the typical case of index range choice staleness because
    // with $or the clauses may likely be logically distinct.)  The greedy
    // implementation won't do any worse than all the $or clauses individually,
    // and it can often do better.  In the first cut we are intentionally using
    // QueryPattern tracking to record successful plans on $or queries for use by
    // subsequent $or queries, even though there may be a significant aggregate
    // $nor component that would not be represented in QueryPattern.
    class MultiPlanScanner {
    public:
        MultiPlanScanner( const char *ns,
                         const BSONObj &query,
                         const BSONObj &order,
                         const BSONElement *hint = 0,
                         bool honorRecordedPlan = true,
                         const BSONObj &min = BSONObj(),
                         const BSONObj &max = BSONObj() );
        shared_ptr< QueryOp > runOp( QueryOp &op );
        template< class T >
        shared_ptr< T > runOp( T &op ) {
            return dynamic_pointer_cast< T >( runOp( static_cast< QueryOp& >( op ) ) );
        }       
        shared_ptr< QueryOp > runOpOnce( QueryOp &op );
        template< class T >
        shared_ptr< T > runOpOnce( T &op ) {
            return dynamic_pointer_cast< T >( runOpOnce( static_cast< QueryOp& >( op ) ) );
        }       
        bool mayRunMore() const { return _i < _n; }
        BSONObj oldExplain() const { assertNotOr(); return _currentQps->explain(); }
        // just report this when only one query op
        bool usingPrerecordedPlan() const {
            return !_or && _currentQps->usingPrerecordedPlan();
        }
        void setBestGuessOnly() { _bestGuessOnly = true; }
    private:
        //temp
        void assertNotOr() const {
            massert( 13266, "not implemented for $or query", !_or );
        }
        // temp (and yucky)
        BSONObj nextSimpleQuery() {
            massert( 13267, "only generate simple query if $or", _or );
            massert( 13270, "no more simple queries", mayRunMore() );
            BSONObjBuilder b;
            BSONArrayBuilder norb;
            BSONObjIterator i( _query );
            while( i.more() ) {
                BSONElement e = i.next();
                if ( strcmp( e.fieldName(), "$nor" ) == 0 ) {
                    massert( 13269, "$nor must be array", e.type() == Array );
                    BSONObjIterator j( e.embeddedObject() );
                    while( j.more() ) {
                        norb << j.next();
                    }
                } else if ( strcmp( e.fieldName(), "$or" ) == 0 ) {
                    BSONObjIterator j( e.embeddedObject() );
                    for( int k = 0; k < _i; ++k ) {
                        norb << j.next();
                    }
                    b << "$or" << BSON_ARRAY( j.next() );
                } else {
                    b.append( e );
                }
            }
            BSONArray nor = norb.arr();
            if ( !nor.isEmpty() ) {
                b << "$nor" << nor;
            }
            ++_i;
            BSONObj ret = b.obj();
            return ret;
        }
        const char * _ns;
        bool _or;
        BSONObj _query;
//        FieldRangeOrSet _fros;
        auto_ptr< QueryPlanSet > _currentQps;
        int _i;
        int _n;
        bool _honorRecordedPlan;
        bool _bestGuessOnly;
    };
    
    class MultiCursor : public Cursor {
    public:
        class CursorOp : public QueryOp {
        public:
            virtual shared_ptr< Cursor > newCursor() const = 0;  
            virtual auto_ptr< CoveredIndexMatcher > newMatcher() const = 0;
        };
        // takes ownership of 'op'
        MultiCursor( const char *ns, const BSONObj &pattern, const BSONObj &order, auto_ptr< CursorOp > op = auto_ptr< CursorOp >( 0 ) )
        : _mps( new MultiPlanScanner( ns, pattern, order ) ) {
            if ( op.get() ) {
                _op = op;
            } else {
                _op.reset( new NoOp() );
                _mps->setBestGuessOnly();
            }
            if ( _mps->mayRunMore() ) {
                nextClause();
                if ( !ok() ) {
                    advance();
                }
            } else {
                _c.reset( new BasicCursor( DiskLoc() ) );
            }
        }
        // used to handoff a query to a getMore()
        MultiCursor( auto_ptr< MultiPlanScanner > mps, const shared_ptr< Cursor > &c, auto_ptr< CoveredIndexMatcher > matcher )
        : _op( new NoOp() ), _c( c ), _mps( mps ), _matcher( matcher ) {
            _mps->setBestGuessOnly();
        }
        virtual bool ok() { return _c->ok(); }
        virtual Record* _current() { return _c->_current(); }
        virtual BSONObj current() { return _c->current(); }
        virtual DiskLoc currLoc() { return _c->currLoc(); }
        virtual bool advance() {
            _c->advance();
            while( !ok() && _mps->mayRunMore() ) {
                nextClause();
            }
            return ok();
        }
        virtual BSONObj currKey() const { return _c->currKey(); }
        virtual DiskLoc refLoc() { return _c->refLoc(); }
        virtual void noteLocation() { _c->noteLocation(); }
        virtual void checkLocation() {
            _c->checkLocation();
            if ( !ok() ) {
                advance();
            }
        }        
        virtual bool supportGetMore() { return true; }
        // with update we could potentially get the same document on multiple
        // indexes, but update appears to already handle this with seenObjects
        // so we don't have to do anything special here.
        virtual bool getsetdup(DiskLoc loc) {
            return _c->getsetdup( loc );   
        }
        virtual CoveredIndexMatcher *matcher() const { return _matcher.get(); }
    private:
        class NoOp : public CursorOp {
            virtual void init() { setComplete(); }
            virtual void next() {}
            virtual bool mayRecordPlan() const { return false; }
            virtual QueryOp *clone() const { return new NoOp(); }
            virtual shared_ptr< Cursor > newCursor() const { return qp().newCursor(); }
            virtual auto_ptr< CoveredIndexMatcher > newMatcher() const {
                return auto_ptr< CoveredIndexMatcher >( new CoveredIndexMatcher( qp().query(), qp().indexKey() ) );
            }
        };
        void nextClause() {
            shared_ptr< CursorOp > best = _mps->runOpOnce( *_op );
            massert( 10401 , best->exceptionMessage(), best->complete() );
            _c = best->newCursor();
            _matcher = best->newMatcher();
        }
        auto_ptr< CursorOp > _op;
        shared_ptr< Cursor > _c;
        auto_ptr< MultiPlanScanner > _mps;
        auto_ptr< CoveredIndexMatcher > _matcher;
    };
    
    // NOTE min, max, and keyPattern will be updated to be consistent with the selected index.
    IndexDetails *indexDetailsForRange( const char *ns, string &errmsg, BSONObj &min, BSONObj &max, BSONObj &keyPattern );

    inline bool isSimpleIdQuery( const BSONObj& query ){
        return 
            strcmp( query.firstElement().fieldName() , "_id" ) == 0 && 
            query.nFields() == 1 && 
            query.firstElement().isSimpleType();
    }
    
    // matcher() will always work on the returned cursor
    inline shared_ptr< Cursor > bestGuessCursor( const char *ns, const BSONObj &query, const BSONObj &sort ) {
        if( !query.getField( "$or" ).eoo() ) {
            return shared_ptr< Cursor >( new MultiCursor( ns, query, sort ) );
        } else {
            shared_ptr< Cursor > ret = QueryPlanSet( ns, query, sort ).getBestGuess()->newCursor();
            if ( !query.isEmpty() ) {
                auto_ptr< CoveredIndexMatcher > matcher( new CoveredIndexMatcher( query, ret->indexKeyPattern() ) );
                ret->setMatcher( matcher );
            }
            return ret;
        }
    }
        
} // namespace mongo
