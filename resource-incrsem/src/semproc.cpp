////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  This file is part of ModelBlocks. Copyright 2009, ModelBlocks developers. //
//                                                                            //
//  ModelBlocks is free software: you can redistribute it and/or modify       //
//  it under the terms of the GNU General Public License as published by      //
//  the Free Software Foundation, either version 3 of the License, or         //
//  (at your option) any later version.                                       //
//                                                                            //
//  ModelBlocks is distributed in the hope that it will be useful,            //
//  but WITHOUT ANY WARRANTY; without even the implied warranty of            //
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             //
//  GNU General Public License for more details.                              //
//                                                                            //
//  You should have received a copy of the GNU General Public License         //
//  along with ModelBlocks.  If not, see <http://www.gnu.org/licenses/>.      //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#define ARMA_64BIT_WORD
#include <iostream>
#include <fstream>
#include <list>
using namespace std;
#include <armadillo>
using namespace arma;
#define DONT_USE_UNMAPPABLE_TUPLES
#include <nl-randvar.h>
#include <nl-string.h>
#include <Delimited.hpp>
bool STORESTATE_TYPE = true;
bool STORESTATE_CHATTY = false;
#include <StoreState.hpp>

uint BEAM_WIDTH = 1000;
uint VERBOSE    = 0;

////////////////////////////////////////////////////////////////////////////////

char psSpcColonSpc[]  = " : ";
char psSpcEqualsSpc[] = " = ";

////////////////////////////////////////////////////////////////////////////////

typedef Delimited<T> P;
typedef Delimited<T> A;
typedef Delimited<T> B;

////////////////////////////////////////////////////////////////////////////////

class BeamElement : public DelimitedSext<psX,Delimited<double>,psSpace,StoreState,psSpace,Sign,psSpace,Delimited<int>,psSpace,F,psSpace,J,psX> {
 public:
  BeamElement ( )                                                               : DelimitedSext<psX,Delimited<double>,psSpace,StoreState,psSpace,Sign,psSpace,Delimited<int>,psSpace,F,psSpace,J,psX>()            { }
  BeamElement ( double d, const StoreState& q, const Sign& a, int i, F f, J j ) : DelimitedSext<psX,Delimited<double>,psSpace,StoreState,psSpace,Sign,psSpace,Delimited<int>,psSpace,F,psSpace,J,psX>(d,q,a,i,f,j) { }
};

class Beam : public DelimitedVector<psX,BeamElement,psLine,psX> {
 public:
  Beam ( )        : DelimitedVector<psX,BeamElement,psLine,psX>()  { }
  Beam ( uint i ) : DelimitedVector<psX,BeamElement,psLine,psX>(i) { }
};

class Trellis : public vector<Beam> {
 private:
  DelimitedList<psX,BeamElement,psLine,psX> lbe;
 public:
  Trellis ( ) : vector<Beam>() { reserve(100); }
  Beam& operator[] ( uint i ) { if ( i==size() ) emplace_back(); return vector<Beam>::operator[](i); }
  const DelimitedList<psX,BeamElement,psLine,psX>& getMostLikelySequence ( ) {
    lbe.clear();  // lbe.push_front( back().front() );
    for( BeamElement& be : back() )  if( be.second().size()==0 ) { lbe.push_front( be ); break; }
    if( lbe.size()>0 )  for( int t=size()-2; t>=0; t-- ) lbe.push_front( at(t).at(lbe.front().fourth()) );
    return lbe;
  }
};

class StreamTrellis : public vector<Beam> {
 public:
  StreamTrellis ( ) : vector<Beam>(2) { }       // previous and next beam.
  Beam&       operator[] ( uint i )       { return vector<Beam>::operator[](i%2); }
  const Beam& operator[] ( uint i ) const { return vector<Beam>::operator[](i%2); }
};


////////////////////////////////////////////////////////////////////////////////

int main ( int nArgs, char* argv[] ) {

  // Define model structures...
  arma::mat matF;
  map<PPredictor,map<P,double>> modP;
  map<W,list<DelimitedPair<psX,WPredictor,psSpace,Delimited<double>,psX>>> lexW;
  map<JPredictor,map<JResponse,double>> modJ;
  map<APredictor,map<A,double>> modA;
  map<BPredictor,map<B,double>> modB;

  { // Define model lists...
    list<DelimitedTrip<psX,FPredictor,psSpcColonSpc,Delimited<FResponse>,psSpcEqualsSpc,Delimited<double>,psX>> lF;
    list<DelimitedTrip<psX,PPredictor,psSpcColonSpc,P,psSpcEqualsSpc,Delimited<double>,psX>> lP;
    list<DelimitedTrip<psX,WPredictor,psSpcColonSpc,W,psSpcEqualsSpc,Delimited<double>,psX>> lW;
    list<DelimitedTrip<psX,JPredictor,psSpcColonSpc,Delimited<JResponse>,psSpcEqualsSpc,Delimited<double>,psX>> lJ;
    list<DelimitedTrip<psX,APredictor,psSpcColonSpc,A,psSpcEqualsSpc,Delimited<double>,psX>> lA;
    list<DelimitedTrip<psX,BPredictor,psSpcColonSpc,B,psSpcEqualsSpc,Delimited<double>,psX>> lB;

    lA.emplace_back( DelimitedTrip<psX,APredictor,psSpcColonSpc,A,psSpcEqualsSpc,Delimited<double>,psX>(APredictor(1,0,1,'N','S',T("T"),T("-")),A("-"),1.0) );      // should be T("S")
    lB.emplace_back( DelimitedTrip<psX,BPredictor,psSpcColonSpc,B,psSpcEqualsSpc,Delimited<double>,psX>(BPredictor(1,0,1,'N','S','1',T("-"),T("S")),B("T"),1.0) );

    // For each command-line flag or model file...
    for ( int a=1; a<nArgs; a++ ) {
      if      ( 0==strcmp(argv[a],"-v") ) VERBOSE = 1;
      else if ( 0==strcmp(argv[a],"-V") ) VERBOSE = 2;
      else if ( 0==strncmp(argv[a],"-b",2) ) BEAM_WIDTH = atoi(argv[a]+2);
      //else if ( string(argv[a]) == "t" ) STORESTATE_TYPE = true;
      else {
        cerr << "Loading model " << argv[a] << "..." << endl;
        // Open file...
        ifstream fin (argv[a], ios::in );
        // Read model lists...
        int linenum = 0;
        while ( fin && EOF!=fin.peek() ) {
          if ( fin.peek()=='F' ) fin >> "F " >> *lF.emplace(lF.end()) >> "\n";
          if ( fin.peek()=='P' ) fin >> "P " >> *lP.emplace(lP.end()) >> "\n";
          if ( fin.peek()=='W' ) fin >> "W " >> *lW.emplace(lW.end()) >> "\n";
          if ( fin.peek()=='J' ) fin >> "J " >> *lJ.emplace(lJ.end()) >> "\n";
          if ( fin.peek()=='A' ) fin >> "A " >> *lA.emplace(lA.end()) >> "\n";
          if ( fin.peek()=='B' ) fin >> "B " >> *lB.emplace(lB.end()) >> "\n";
          if ( ++linenum%1000000==0 ) cerr << "  " << linenum << " items loaded..." << endl;
        }
        cerr << "Model " << argv[a] << " loaded." << endl;
      }
    }

    // Populate model structures...
    matF = arma::mat( FResponse::getDomain().getSize(), FPredictor::getDomainSize() );
    for ( auto& prw : lF ) matF( prw.second().toInt(), prw.first().toInt() ) = prw.third();
    for ( auto& prw : lP ) modP[prw.first()][prw.second()] = prw.third();
    for ( auto& prw : lW ) lexW[prw.second()].emplace_back(prw.first(),prw.third());
    for ( auto& prw : lJ ) modJ[prw.first()][prw.second()] = prw.third();
    for ( auto& prw : lA ) modA[prw.first()][prw.second()] = prw.third();
    for ( auto& prw : lB ) modB[prw.first()][prw.second()] = prw.third();
  }

  cerr<<"Models ready."<<endl;

  // For each line in stdin...
  for ( int linenum=1; cin && EOF!=cin.peek(); linenum++ ) {

    Trellis                                beams;  // sequence of beams
    uint                                   t=0;    // time step
    DelimitedList<psX,ObsWord,psSpace,psX> lwSent; // input list

    // Allocate space in beams to avoid reallocation...
    for ( auto& b : beams ) b.reserve(1000);
    // Create initial beam element...
    beams[0].emplace_back();
    // Read sentence...
    cin >> lwSent >> "\n";
    cerr << "#" << linenum;

    // For each word...
    for ( auto& w_t : lwSent ) {

      cerr << " " << w_t;
      if ( VERBOSE ) cout << "WORD:" << w_t << endl;

      // Create beam for current time step...
      beams[++t].clear();

      // For each hypothesized storestate at previous time step...
      for ( auto& be_tdec1 : beams[t-1] ) {
        double            lgpr_tdec1 = be_tdec1.first();   // prob of prev storestate
        const StoreState& q_tdec1    = be_tdec1.second();  // prev storestate

        if ( VERBOSE>1 ) cout << "  " << be_tdec1 << endl;

        // Calc distrib over response for each fork predictor...
        arma::vec fresponses = arma::zeros( matF.n_rows );
        list<FPredictor> lfpredictors;  q_tdec1.calcForkPredictors(lfpredictors);  lfpredictors.emplace_back();
//        for ( auto& fpredr : q_tdec1.calcForkPredictors(lfpredictors) ) if ( fpredr != FPredictor::FPREDR_FAIL ) fresponses += matF.col( fpredr.toInt() );
        for ( auto& fpredr : lfpredictors ) if ( fpredr.toInt() < matF.n_cols ) fresponses += matF.col( fpredr.toInt() );
        if ( VERBOSE>1 ) for ( auto& fpredr : lfpredictors ) cout<<"    fpredr:"<<fpredr<<endl;
        fresponses = arma::exp( fresponses );

        // Calc normalization term over responses...
        double fnorm = arma::accu( fresponses );

        // For each possible lemma (context + label + prob) for preterminal of current word...
        for ( auto& ktpr_p_t : (lexW.end()!=lexW.find(w_t)) ? lexW[w_t] : lexW[unkWord(w_t.getString().c_str())] ) {
          K k_p_t           = ktpr_p_t.first.first;   // context of current preterminal
          T t_p_t           = ktpr_p_t.first.second;  // label of cunnent preterminal
          E e_p_t           = (t_p_t.getLastNonlocal()==N_NONE) ? 'N' : (t_p_t.getLastNonlocal()==N("-rN")) ? '0' : (t_p_t.getLastNonlocal().isArg()) ? t_p_t.getArity()+'1' : 'M';
          double probwgivkl = ktpr_p_t.second;        // probability of current word given current preterminal

          if ( VERBOSE>1 ) cout << "     W " << k_p_t << " " << t_p_t << " : " << w_t << " = " << probwgivkl << endl;

          // For each possible no-fork or fork decision...
          for ( auto& f : {0,1} ) {
            double scoreFork = ( uint(FResponse(f,e_p_t,k_p_t).toInt())<fresponses.n_rows ) ? fresponses(FResponse(f,e_p_t,k_p_t).toInt()) : 1.0 ;
            if ( VERBOSE>1 ) cout << "      F ... : " << f << " " << e_p_t << " " << k_p_t << " = " << (scoreFork / fnorm) << endl;

            // If preterminal prob is nonzero... 
            PPredictor ppredictor = q_tdec1.calcPretrmTypeCondition(f,e_p_t,k_p_t);
            if ( VERBOSE>1 ) cout << "      P " << ppredictor << "..." << endl;
            if ( modP[ppredictor].end()!=modP[ppredictor].find(t_p_t) ) {

              if ( VERBOSE>1 ) cout << "      P " << ppredictor << " : " << t_p_t << " = " << modP[ppredictor][t_p_t] << endl;

              // Calc probability for fork phase...
              double probFork = (scoreFork / fnorm) * modP[q_tdec1.calcPretrmTypeCondition(f,e_p_t,k_p_t)][t_p_t] * probwgivkl;
              if ( VERBOSE>1 ) cout << "      f: " << f <<"&"<< k_p_t << " " << scoreFork << " / " << fnorm << " * " << modP[q_tdec1.calcPretrmTypeCondition(f,e_p_t,k_p_t)][t_p_t] << " * " << probwgivkl << " = " << probFork << endl;

              //NOT USED! const Sign& aAncstr = q_tdec1.getAncstr( f );                                        // aAncstr (brink of previous incomplete cat
              Sign aPretrm;  aPretrm.first.emplace_back(k_p_t);  aPretrm.second = t_p_t;           // aPretrm (pos tag)
              //NOT USED! Sign aLchildTmp;  const Sign& aLchild = q_tdec1.getLchild( aLchildTmp, f, e_p_t, aPretrm ); // aLchild (completed most subordinate apex)
              list<JPredictor> jpredictors; q_tdec1.calcJoinPredictors( jpredictors, f, e_p_t, aPretrm ); // predictors for join
              jpredictors.emplace_back();                                                               // add bias
              double jnorm = 0.0;                                                                         // join normalization term (denominator)
              int iFirstUnnormed = beams[t].size();                                                       // place in beam where normalization begins
              // For each possible no-join or join decision...
              for ( auto& j : {0,1} ) {

                // For each possible extraction operation...
                for ( auto& e : {'N','M','1','2','3'} ) {
                  // For each possible left-child operation...
                  for ( auto& opL : {'1','2','3','I','M','N','S'} ) {

                    // For each possible apex category label...
                    APredictor apredictor = q_tdec1.calcApexTypeCondition( f, j, e_p_t, e, opL, aPretrm );  // save apredictor for use in prob calc
                    if ( VERBOSE>1 ) cout << "         A " << apredictor << "..." << endl;
                    for ( auto& tpA : modA[apredictor] ) {

                      if ( VERBOSE>1 ) cout << "         A " << apredictor << " : " << tpA.first << " = " << tpA.second << endl;

                      // For each possible right-child operation...
                      for ( auto& opR : {'1','2','3','I','M','N'} ) {

                        // Calculate sum of weights for join predictors...
                        if ( VERBOSE>1 ) for ( auto& jpredr : jpredictors ) cout<<"          jpredr:"<<jpredr<<" : "<<JResponse(j,e,opL,opR)<<" = "<<modJ[jpredr][JResponse(j,e,opL,opR)]<<endl;
//                        double logscoreJ = 0.0;  for ( auto& jpredr : jpredictors ) if ( jpredr != JPredictor::JPREDR_FAIL )  logscoreJ += modJ[jpredr][JResponse(j,e,opL,opR)];
                        double logscoreJ = 0.0;  for ( auto& jpredr : jpredictors ) if ( modJ.end() != modJ.find(jpredr) )  logscoreJ += modJ[jpredr][JResponse(j,e,opL,opR)];

                        // For each possible brink category label...
                        BPredictor bpredictor = q_tdec1.calcBrinkTypeCondition( f, j, e_p_t, e, opL, opR, tpA.first, aPretrm );  // bpredictor for prob calc
                        if ( VERBOSE>1 ) cout << "          B " << bpredictor << "..." << endl;
                        for ( auto& tpB : modB[bpredictor] ) {

//                        if ( VERBOSE>1 ) cout << "            B " << bpredictor << " : " << tpB.first << " = " << tpB.second << endl;

                          // Calculate probability and storestate and add to beam...
                          double scoreJoin = exp(logscoreJ) * tpA.second * tpB.second;  // save score for jnorm update
                          beams[t].emplace_back( scoreJoin, StoreState( q_tdec1, f, j, e_p_t, e, opL, opR, tpA.first, tpB.first, aPretrm ), aPretrm, &be_tdec1-&beams[t-1][0], f, j );
                          // Update join normalization term...
                          jnorm += scoreJoin;

                          if ( VERBOSE>1 ) cout << "            " << q_tdec1 << "  ==(f" << f << ",j" << j << ",e_p_t" << e_p_t << ",e" << e << "," << opL << "," << opR << "," << tpA.first << "," << tpB.first << "," << aPretrm << ")=>  " << beams[t].back().second() << "  q" << lgpr_tdec1 << " f" << log(probFork) << " (j" << logscoreJ << " a" << log(tpA.second) << " b" << log(tpB.second) << " = " << log(scoreJoin) << ")" << endl;
                        }
                      }
                    }
                  }
                }
              }
              // Normalize join probabilities...
              if ( VERBOSE>1 ) cout << "        renorm " << iFirstUnnormed << " " << beams[t].size() << endl;
              for ( auto it=beams[t].begin()+iFirstUnnormed; it!=beams[t].end(); it++ )  it->first() = lgpr_tdec1 + log(probFork) + log(it->first()) - log(jnorm);
            }
          }
          if ( VERBOSE>1 ) cout << "      end Fs" << endl;
        }
        if ( VERBOSE>1 ) cout << "    end lemmas" << endl; 
      }

      if ( VERBOSE>1 ) cout << "with spurious sentence breaks: beams[t].size()=" << beams[t].size() << endl;

      // Remove sentence breaks before end of sentence...
      if ( t != lwSent.size() )
        for ( auto it=beams[t].begin(); it<beams[t].end(); it++ )
          if ( it->second().size()==0 ) beams[t].erase(it);

      if ( VERBOSE>1 ) cout << "without spurious sentence breaks: beams[t].size()=" << beams[t].size() << endl;

      // Sort and truncate beam...
      std::sort( beams[t].begin(), beams[t].end(), [] (const BeamElement& a, const BeamElement& b) { return b<a; } );
      if ( beams[t].size() > BEAM_WIDTH ) beams[t].erase( beams[t].begin()+BEAM_WIDTH, beams[t].end() );

      // Write output...
      cerr << " (" << beams[t].size() << ")";
      if ( VERBOSE ) cout << beams[t] << endl;
    }
    cerr << endl;
    if ( VERBOSE ) cout << "MLS" << endl;

    /// cout << beams.getMostLikelySequence() << endl;
    auto& mls = beams.getMostLikelySequence();
    auto ibe=next(mls.begin()); auto iw=lwSent.begin(); for ( ; ibe!=mls.end() && iw!=lwSent.end(); ibe++, iw++ )
      cout << *iw << " " << ibe->third() << " " << ibe->fifth() << " " << ibe->sixth() << " " << ibe->second() << endl;
    if ( mls.size()==0 ) cout << "FAIL FAIL 1 1 " << endl;
  }
}
