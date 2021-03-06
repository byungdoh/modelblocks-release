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
#include <thread>
#include <mutex>
#include <chrono>
using namespace std;
#include <armadillo>
using namespace arma;
#define DONT_USE_UNMAPPABLE_TUPLES
#include <nl-randvar.h>
#include <nl-string.h>
#include <Delimited.hpp>
bool STORESTATE_TYPE = true;
bool STORESTATE_CHATTY = false;
uint FEATCONFIG = 0;
#include <StoreState.hpp>
#include <Beam.hpp>

uint BEAM_WIDTH      = 1000;
uint VERBOSE         = 0;
uint OUTPUT_MEASURES = 0;

////////////////////////////////////////////////////////////////////////////////

char psSpcColonSpc[]  = " : ";
char psSpcEqualsSpc[] = " = ";
char psSpaceF[]       = " f";
char psAmpersand[]    = "&";

////////////////////////////////////////////////////////////////////////////////

typedef Delimited<T> P;
typedef Delimited<T> A;
typedef Delimited<T> B;

////////////////////////////////////////////////////////////////////////////////

class BeamElement : public DelimitedSext<psX,Sign,psSpaceF,F,psAmpersand,E,psAmpersand,K,psSpace,JResponse,psSpace,StoreState,psX> {
public:
  BeamElement ( )                                                                 : DelimitedSext<psX,Sign,psSpaceF,F,psAmpersand,E,psAmpersand,K,psSpace,JResponse,psSpace,StoreState,psX>()             { }
  BeamElement ( const Sign& a, F f, E e, K k, JResponse jr, const StoreState& q ) : DelimitedSext<psX,Sign,psSpaceF,F,psAmpersand,E,psAmpersand,K,psSpace,JResponse,psSpace,StoreState,psX>(a,f,e,k,jr,q) { }
};
const BeamElement beStableDummy; //equivalent to "beStableDummy = BeamElement()"

//typedef pair<double,const BeamElement&> ProbBack;
class ProbBack : public pair<double, const BeamElement&> {
 public :
  ProbBack ( )                                  : pair<double, const BeamElement&> ( 0.0, beStableDummy ) { }
  ProbBack ( double d , const BeamElement& be ) : pair<double, const BeamElement&> ( d,   be            ) { }
};

class Trellis : public vector<Beam<ProbBack,BeamElement>> {
  // private:
  //  DelimitedList<psX,pair<BeamElement,ProbBack>,psLine,psX> lbe;
public:
  Trellis ( ) : vector<Beam<ProbBack,BeamElement>>() { reserve(100); }
  Beam<ProbBack,BeamElement>& operator[] ( uint i ) { if ( i==size() ) emplace_back(BEAM_WIDTH); return vector<Beam<ProbBack,BeamElement>>::operator[](i); }
  void setMostLikelySequence ( DelimitedList<psX,pair<BeamElement,ProbBack>,psLine,psX>& lbe ) {
    static StoreState ssLongFail( StoreState(), 1, 0, 'N', 'N', 'N', 'I', "FAIL", "FAIL", Sign(ksBot,"FAIL",0), Sign(ksBot,"FAIL",0) );
    lbe.clear();  if( back().size()>0 ) lbe.push_front( pair<BeamElement,ProbBack>( back().begin()->second, back().begin()->first ) );
    if( lbe.size()>0 ) for( int t=size()-2; t>=0; t-- ) lbe.push_front( at(t).get(lbe.front().second.second) );
    if( lbe.size()>0 ) lbe.emplace_back( BeamElement(), ProbBack(0.0,BeamElement()) );
    // If parse fails...
    if( lbe.size()==0 ) {
      // Print a right branching structure...
      for( int t=size()-2; t>=0; t-- ) lbe.push_front( pair<BeamElement,ProbBack>( BeamElement( Sign(ksBot,"FAIL",0), 1, 'N', K::kBot, JResponse(1,'N','N','I'), ssLongFail ), ProbBack(0.0,BeamElement()) ) );
      lbe.front().first = BeamElement( Sign(ksBot,"FAIL",0), 1, 'N', K::kBot, JResponse(0,'N','N','I'), ssLongFail );                    // front: fork no-join
      lbe.back( ).first = BeamElement( Sign(ksBot,"FAIL",0), 0, 'N', K::kBot, JResponse(1,'N','N','I'), StoreState() );                  // back: join no-fork
      if( size()==2 ) lbe.front().first = BeamElement( Sign(ksBot,"FAIL",0), 1, 'N', K::kBot, JResponse(1,'N','N','I'), StoreState() );  // unary case: fork and join
      // Add dummy element (not sure why this is needed)...
      lbe.push_front( pair<BeamElement,ProbBack>( BeamElement( Sign(ksBot,"FAIL",0), 0, 'N', K::kBot, JResponse(0,'N','N','I'), StoreState() ), ProbBack(0.0,BeamElement()) ) );
      cerr<<"parse failed"<<endl;
    }
    // For each element of MLE after first dummy element...
    int u=-1; for( auto& be : lbe ) if( ++u>0 and u<int(size()) ) {
      // Calc surprisal as diff in exp of beam totals of successive elements, minus constant...
      double probPrevTot = 0.0;
      double probCurrTot = 0.0;
      for( auto& beP : at(u-1) ) probPrevTot += exp( beP.first.first - at(u-1).begin()->first.first );
      for( auto& beC : at(u  ) ) probCurrTot += exp( beC.first.first - at(u-1).begin()->first.first ); 
      be.second.first = log2(probPrevTot) - log2(probCurrTot);     // store surp into prob field of beam item
    }
    //    return lbe;
  }
};

/*
 class StreamTrellis : public vector<Beam> {
 public:
 StreamTrellis ( ) : vector<Beam>(2) { }       // previous and next beam.
 Beam&       operator[] ( uint i )       { return vector<Beam>::operator[](i%2); }
 const Beam& operator[] ( uint i ) const { return vector<Beam>::operator[](i%2); }
 };
 */

////////////////////////////////////////////////////////////////////////////////

int main ( int nArgs, char* argv[] ) {

  uint numThreads = 1;

  // Define model structures...
  arma::mat matE;
  arma::mat matF;
  arma::mat matJ;
  map<PPredictor,map<P,double>> modP;
  map<W,list<DelimitedPair<psX,WPredictor,psSpace,Delimited<double>,psX>>> lexW;
  map<APredictor,map<A,double>> modA;
  map<BPredictor,map<B,double>> modB;

  { // Define model lists...
    list<DelimitedTrip<psX,FPredictor,psSpcColonSpc,Delimited<int>,psSpcEqualsSpc,Delimited<double>,psX>> lE;
    list<DelimitedTrip<psX,Delimited<int>,psSpcColonSpc,Delimited<FResponse>,psSpcEqualsSpc,Delimited<double>,psX>> lF;
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
      else if ( 0==strncmp(argv[a],"-p",2) ) numThreads = atoi(argv[a]+2);
      else if ( 0==strncmp(argv[a],"-b",2) ) BEAM_WIDTH = atoi(argv[a]+2);
      else if ( 0==strcmp(argv[a],"-c") ) OUTPUT_MEASURES = 1;
      else if ( 0==strncmp(argv[a],"-f",2) ) FEATCONFIG = atoi(argv[a]+2);
      //else if ( string(argv[a]) == "t" ) STORESTATE_TYPE = true;
      else {
        cerr << "Loading model " << argv[a] << "..." << endl;
        // Open file...
        ifstream fin (argv[a], ios::in );
        // Read model lists...
        int linenum = 0;
        while ( fin && EOF!=fin.peek() ) {
          if ( fin.peek()=='E' ) fin >> "E " >> *lE.emplace(lE.end()) >> "\n";
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
    // CODE REVIEW: Infer embedding dim
    int emb_dim = 128;
    matE = arma::zeros( emb_dim, FPredictor::getDomainSize() );
    matF = arma::zeros( FResponse::getDomain().getSize(), emb_dim );
    matJ = arma::zeros( JResponse::getDomain().getSize(), JPredictor::getDomainSize() );
    for ( auto& prw : lE ) matE( prw.second(), prw.first().toInt() ) = prw.third();
    for ( auto& prw : lF ) matF( prw.second().toInt(), prw.first() ) = prw.third();
    for ( auto& prw : lP ) modP[prw.first()][prw.second()] = prw.third();
    for ( auto& prw : lW ) lexW[prw.second()].emplace_back(prw.first(),prw.third());
    for ( auto& prw : lJ ) matJ( prw.second().toInt(), prw.first().toInt() ) = prw.third();
    for ( auto& prw : lA ) modA[prw.first()][prw.second()] = prw.third();
    for ( auto& prw : lB ) modB[prw.first()][prw.second()] = prw.third();
  }

  // Add unk...
  for( auto& entry : lexW ) {
    // for each word:{<category:prob>}
    for( auto& unklistelem : lexW[unkWord(entry.first.getString().c_str())] ) {
      // for each possible unked(word) category:prob pair
      bool BAIL = false;
      for( auto& listelem : entry.second ) {
        if (listelem.first == unklistelem.first) {
          BAIL = true;
          listelem.second = listelem.second + ( 0.000001 * unklistelem.second ); // merge actual likelihood and unk likelihood
        }
      }
      if (not BAIL) entry.second.push_back( DelimitedPair<psX,WPredictor,psSpace,Delimited<double>,psX>(unklistelem.first,0.000001*unklistelem.second) );
    }
  }

  cerr<<"Models ready."<<endl;

  list<DelimitedList<psX,pair<BeamElement,ProbBack>,psLine,psX>> MLSs;
  list<DelimitedList<psX,ObsWord,psSpace,psX>> sents;
  mutex mutexMLSList;
  vector<thread> vtWorkers;  vtWorkers.reserve( numThreads );
  uint linenum = 1;

  if( OUTPUT_MEASURES ) cout << "word pos f j store totsurp" << endl;

  // For each line in stdin...
  //  for ( int linenum=1; cin && EOF!=cin.peek(); linenum++ ) {
  for( uint numtglobal=0; numtglobal<numThreads; numtglobal++ ) vtWorkers.push_back( thread( [&MLSs,&sents,&mutexMLSList,&linenum,numThreads,matE,matF,modP,lexW,matJ,modA,modB] (uint numt) {

    auto tpLastReport = chrono::high_resolution_clock::now();
    
    while( true ) {

      Trellis                                beams;  // sequence of beams
      uint                                   t=0;    // time step
      DelimitedList<psX,ObsWord,psSpace,psX> lwSent; // input list

      // Allocate space in beams to avoid reallocation...
      // Create initial beam element...
      beams[0].tryAdd( BeamElement(), ProbBack(0.0,BeamElement()) );

      mutexMLSList.lock( );
      if( not ( cin && EOF!=cin.peek() ) ) { mutexMLSList.unlock(); break; }
      // Read sentence...
      uint currline = linenum++;
      cin >> lwSent >> "\n";
      cerr << "Reading sentence " << currline << ": " << lwSent << " ..." << endl;
      // Add mls to list...
      MLSs.emplace_back( );
      auto& mls = MLSs.back();
      sents.emplace_back( lwSent );
      mutexMLSList.unlock();

      if ( numThreads == 1 ) cerr << "#" << currline;

      // For each word...
      for ( auto& w_t : lwSent ) {

        if ( numThreads == 1 ) cerr << " " << w_t;
        if ( VERBOSE ) cout << "WORD:" << w_t << endl;

        // Create beam for current time step...
        beams[++t].clear();

        //      mutex mutexBeam;
        //      vector<thread> vtWorkers;
        //      for( uint numtglobal=0; numtglobal<numThreads; numtglobal++ ) vtWorkers.push_back( thread( [&] (uint numt) {

        // For each hypothesized storestate at previous time step...
        uint i=0; for( auto& be_tdec1 : beams[t-1] ) {
          //         if( i++%numThreads==numt ){
          double            lgpr_tdec1 = be_tdec1.first.first;      // prob of prev storestate
          const StoreState& q_tdec1    = be_tdec1.second.sixth();  // prev storestate

          if( VERBOSE>1 ) cout << "  from (" << be_tdec1.second << ")" << endl;

          // Calc distrib over response for each fork predictor...
          arma::vec flogresponses = arma::zeros( matF.n_rows );
          list<FPredictor> lfpredictors;  q_tdec1.calcForkPredictors( lfpredictors, false );  lfpredictors.emplace_back();  // add bias term
          for ( auto& fpredr : lfpredictors ) if ( fpredr.toInt() < matF.n_cols ) flogresponses += matF * matE.col( fpredr.toInt() ); // add logprob for all indicated features. over all FEK responses.
          if ( VERBOSE>1 ) for ( auto& fpredr : lfpredictors ) cout<<"    fpredr:"<<fpredr<<endl;
          arma::vec fresponses = arma::exp( flogresponses );
          // Calc normalization term over responses...
          double fnorm = arma::accu( fresponses );

          // Rescale overflowing distribs by max...
          if( fnorm == 1.0/0.0 ) {
            uint ind_max=0; for( i=0; i<fresponses.size(); i++ ) if( fresponses(i)>fresponses(ind_max) ) ind_max=i;
            flogresponses -= flogresponses( ind_max );
            fresponses = arma::exp( flogresponses );
            fnorm = arma::accu( fresponses );
            //            fresponses.fill( 0.0 );  fresponses( ind_max ) = 1.0;
            //            fnorm = 1.0;
          }

          // For each possible lemma (context + label + prob) for preterminal of current word...
          if( lexW.end() == lexW.find(unkWord(w_t.getString().c_str())) ) cerr<<"ERROR: unable to find unk form: "<<unkWord(w_t.getString().c_str())<<endl;
          for ( auto& ktpr_p_t : (lexW.end()!=lexW.find(w_t)) ? lexW.find(w_t)->second : lexW.find(unkWord(w_t.getString().c_str()))->second ) {
            if( beams[t].size()<BEAM_WIDTH || lgpr_tdec1 + log(ktpr_p_t.second) > beams[t].rbegin()->first.first ) {
              K k_p_t           = (FEATCONFIG & 8 && ktpr_p_t.first.first.getString()[2]!='y') ? K::kBot : ktpr_p_t.first.first;   // context of current preterminal
              T t_p_t           = ktpr_p_t.first.second;                               // label of current preterminal
              E e_p_t           = (t_p_t.getLastNonlocal()==N_NONE) ? 'N' : (t_p_t.getLastNonlocal()==N("-rN")) ? '0' : (t_p_t.getLastNonlocal().isArg()) ? t_p_t.getArity()+'1' : 'M';
              double probwgivkl = ktpr_p_t.second;                                     // probability of current word given current preterminal

              if ( VERBOSE>1 ) cout << "     W " << k_p_t << " " << t_p_t << " : " << w_t << " = " << probwgivkl << endl;

              // For each possible no-fork or fork decision...
              for ( auto& f : {0,1} ) {
                if( FResponse::exists(f,e_p_t,k_p_t) && FResponse(f,e_p_t,k_p_t).toInt() >= int(fresponses.size()) ) cerr<<"ERROR: unable to find fresponse "<<FResponse(f,e_p_t,k_p_t)<<endl;
                double scoreFork = ( FResponse::exists(f,e_p_t,k_p_t) ) ? fresponses(FResponse(f,e_p_t,k_p_t).toInt()) : 1.0 ;
                if ( VERBOSE>1 ) cout << "      F ... : " << f << " " << e_p_t << " " << k_p_t << " = " << (scoreFork / fnorm) << endl;

                if( chrono::high_resolution_clock::now() > tpLastReport + chrono::minutes(1) ) {
                  tpLastReport = chrono::high_resolution_clock::now();
                  lock_guard<mutex> guard( mutexMLSList );
                  cerr << "WORKER " << numt << ": SENT " << currline << " WORD " << t << " FROM " << be_tdec1.second << " PRED " << ktpr_p_t << endl;
                }

                // If preterminal prob is nonzero...
                PPredictor ppredictor = q_tdec1.calcPretrmTypeCondition(f,e_p_t,k_p_t);
                if ( VERBOSE>1 ) cout << "      P " << ppredictor << "..." << endl;
                if ( modP.end()!=modP.find(ppredictor) && modP.find(ppredictor)->second.end()!=modP.find(ppredictor)->second.find(t_p_t) ) {

                  if ( VERBOSE>1 ) cout << "      P " << ppredictor << " : " << t_p_t << " = " << modP.find(ppredictor)->second.find(t_p_t)->second << endl;

                  // Calc probability for fork phase...
                  double probFork = (scoreFork / fnorm) * modP.find(ppredictor)->second.find(t_p_t)->second * probwgivkl;
                  if ( VERBOSE>1 ) cout << "      f: f" << f << "&" << e_p_t << "&" << k_p_t << " " << scoreFork << " / " << fnorm << " * " << modP.find(ppredictor)->second.find(t_p_t)->second << " * " << probwgivkl << " = " << probFork << endl;

                  Sign aPretrm;  aPretrm.first().emplace_back(k_p_t);  aPretrm.second() = t_p_t;  aPretrm.third() = S_A;          // aPretrm (pos tag)
                  const LeftChildSign aLchild( q_tdec1, f, e_p_t, aPretrm );
                  list<JPredictor> ljpredictors; q_tdec1.calcJoinPredictors( ljpredictors, f, e_p_t, aLchild, false ); // predictors for join
                  ljpredictors.emplace_back();                                                                  // add bias
                  arma::vec jlogresponses = arma::zeros( matJ.n_rows );
                  for ( auto& jpredr : ljpredictors ) if ( jpredr.toInt() < matJ.n_cols ) jlogresponses += matJ.col( jpredr.toInt() );
                  arma::vec jresponses = arma::exp( jlogresponses );
                  double jnorm = arma::accu( jresponses );  // 0.0;                                           // join normalization term (denominator)

                  // Replace overflowing distribs by max...
                  if( jnorm == 1.0/0.0 ) {
                    uint ind_max=0; for( i=0; i<jlogresponses.size(); i++ ) if( jlogresponses(i)>jlogresponses(ind_max) ) ind_max=i;
                    jlogresponses -= jlogresponses( ind_max );
                    jresponses = arma::exp( jlogresponses );
                    jnorm = arma::accu( jresponses );
                    //                    jresponses.fill( 0.0 );  jresponses( ind_max ) = 1.0;
                    //                    jnorm = 1.0;
                  }

                  // For each possible no-join or join decision, and operator decisions...
                  for( JResponse jresponse; jresponse<JResponse::getDomain().getSize(); ++jresponse ) {
                    if( beams[t].size()<BEAM_WIDTH || lgpr_tdec1 + log(probFork) + log(jresponses[jresponse.toInt()]/jnorm) > beams[t].rbegin()->first.first ) {
                      J j   = jresponse.getJoin();
                      E e   = jresponse.getE();
                      O opL = jresponse.getLOp();
                      O opR = jresponse.getROp();
                      if( jresponse.toInt() >= int(jresponses.size()) ) cerr<<"ERROR: unknown jresponse: "<<jresponse<<endl;
                      double probJoin = jresponses[jresponse.toInt()] / jnorm;
                      if ( VERBOSE>1 ) cout << "       J ... " << " : " << jresponse << " = " << probJoin << endl;

                      // For each possible apex category label...
                      APredictor apredictor = q_tdec1.calcApexTypeCondition( f, j, e_p_t, e, opL, aLchild );  // save apredictor for use in prob calc
                      if ( VERBOSE>1 ) cout << "         A " << apredictor << "..." << endl;
                      if ( modA.end()!=modA.find(apredictor) )
                        for ( auto& tpA : modA.find(apredictor)->second ) {
                          if( beams[t].size()<BEAM_WIDTH || lgpr_tdec1 + log(probFork) + log(probJoin) + log(tpA.second) > beams[t].rbegin()->first.first ) {

                            if ( VERBOSE>1 ) cout << "         A " << apredictor << " : " << tpA.first << " = " << tpA.second << endl;

                            // For each possible brink category label...
                            BPredictor bpredictor = q_tdec1.calcBrinkTypeCondition( f, j, e_p_t, e, opL, opR, tpA.first, aLchild );  // bpredictor for prob calc
                            if ( VERBOSE>1 ) cout << "          B " << bpredictor << "..." << endl;
                            if ( modB.end()!=modB.find(bpredictor) )
                              for ( auto& tpB : modB.find(bpredictor)->second ) {
                                if ( VERBOSE>1 ) cout << "          B " << bpredictor << " : " << tpB.first << " = " << tpB.second << endl;
                                //                            lock_guard<mutex> guard( mutexBeam );
                                if( beams[t].size()<BEAM_WIDTH || lgpr_tdec1 + log(probFork) + log(probJoin) + log(tpA.second) + log(tpB.second) > beams[t].rbegin()->first.first ) {

                                  if( chrono::high_resolution_clock::now() > tpLastReport + chrono::minutes(1) ) {
                                    tpLastReport = chrono::high_resolution_clock::now();
                                    lock_guard<mutex> guard( mutexMLSList );
                                    cerr << "WORKER " << numt << ": SENT " << currline << " WORD " << t << " FROM " << be_tdec1.second << " PRED " << ktpr_p_t << " JRESP " << jresponse << " A " << tpA.first << " B " << tpB.first << endl;
                                  }

                                  // Calculate probability and storestate and add to beam...
                                  StoreState ss( q_tdec1, f, j, e_p_t, e, opL, opR, tpA.first, tpB.first, aPretrm, aLchild );
                                  if( (t<lwSent.size() && ss.size()>0) || (t==lwSent.size() && ss.size()==0) ) {
                                    beams[t].tryAdd( BeamElement( aPretrm, f,e_p_t,k_p_t, jresponse, ss ), ProbBack( lgpr_tdec1 + log(probFork) + log(probJoin) + log(tpA.second) + log(tpB.second), be_tdec1.second ) );
                                    if( VERBOSE>1 ) cout << "                send (" << be_tdec1.second << ") to (" << ss << ") with "
                                      << (lgpr_tdec1 + log(probFork) + log(probJoin) + log(tpA.second) + log(tpB.second)) << endl;
                                  }
                                }
                              }
                          }
                        }
                    }
                  }
                }
              }
            }
          }
        }
        // Write output...
        if ( numThreads == 1 ) cerr << " (" << beams[t].size() << ")";
        if ( VERBOSE ) cout << beams[t] << endl;
        { lock_guard<mutex> guard( mutexMLSList );
          cerr << "WORKER " << numt << ": SENT " << currline << " WORD " << t << endl;	
        }
      }
      if ( numThreads == 1 ) cerr << endl;
      if ( VERBOSE ) cout << "MLS" << endl;

      //DelimitedList<psX,pair<BeamElement,ProbBack>,psLine,psX> mls;
      { lock_guard<mutex> guard( mutexMLSList );
        if( numThreads > 1 ) cerr << "Finished line " << currline << " (" << beams[t].size() << ")..." << endl;
        beams.setMostLikelySequence( mls );

       /*
       if( mls.size()>0 ) {
       int u=1; auto ibe=next(mls.begin()); auto iw=lwSent.begin(); for( ; ibe!=mls.end() && iw!=lwSent.end(); ibe++, iw++, u++ ) {
       cout << *iw << " " << ibe->first;
       //ibe->first.first() << " " << ibe->first.second() << " " << ibe->first.third() << " " << ibe->first.fourth();
       //        if( OUTPUT_MEASURES ) cout << " " << vdSurp[u];
       cout << endl;
       }
       }
       if( mls.size()==0 ) {
       cout << "FAIL FAIL 1 1 " << endl;
       //      int u=1; auto iw=lwSent.begin(); for( ; iw!=lwSent.end(); iw++, u++ ) {
       //        cout << *iw << " FAIL 1 1 ";
       //        if( OUTPUT_MEASURES ) cout << " " << vdSurp[u];
       //        cout << endl;
       //      }
       }
       */

//        for( auto& mls : MLSs )// cerr<<"on list: "<<mls.size()<<endl;
//          for( auto& be : mls ) cerr<<"on list: " << be.first << endl;
        while( MLSs.size()>0 && MLSs.front().size()>0 ) {
          auto& mls = MLSs.front( );
          int u=1; auto ibe=next(mls.begin()); auto iw=sents.front().begin(); for( ; ibe!=mls.end() && iw!=sents.front().end(); ibe++, iw++, u++ ) {
            cout << *iw << " " << ibe->first << " " << ibe->second.first;
            cout << endl;
          }
          MLSs.pop_front();
          sents.pop_front();
        }
      }
    }
  }, numtglobal ));

  for( auto& w : vtWorkers ) w.join();

}

