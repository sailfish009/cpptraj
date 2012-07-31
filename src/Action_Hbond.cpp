// Action_Hbond 
#include <cmath> // sqrt
#include <algorithm> // sort
#include "Action_Hbond.h"
#include "CpptrajStdio.h"
#include "Constants.h" // RADDEG, DEGRAD

// CONSTRUCTOR
Action_Hbond::Action_Hbond() :
  Nframes_(0),
  hasDonorMask_(false),
  hasAcceptorMask_(false),
  hasSolventDonor_(false),
  hasSolventAcceptor_(false),
  calcSolvent_(false),
  acut_(0),
  dcut2_(0),
  series_(false),
  NumHbonds_(NULL),
  NumSolvent_(NULL),
  NumBridge_(NULL)
{}

// Action_Hbond::init()
/** Expected call: hbond [out <filename>] <mask> [angle <cut>] [dist <cut>] [series]
  *                      [donormask <mask>] [acceptormask <mask>] [avgout <filename>]
  *                      [solventdonor <mask>] [solventacceptor <mask>] 
  *                      [solvout <filename>] [bridgeout <filename>]
  * Search for Hbonding atoms in region specified by mask. 
  * Arg. check order is:
  * - Keywords
  * - Masks
  * If just <mask> is specified donors and acceptors will be automatically
  * searched for.
  * If donormask is specified but not acceptormask, acceptors will be 
  * automatically searched for in <mask>.
  * If acceptormask is specified but not donormask, donors will be automatically
  * searched for in <mask>.
  * If both donormask and acceptor mask are specified no searching will occur.
  */
int Action_Hbond::init() {
  // Get keywords
  char* outfilename = actionArgs.getKeyString("out",NULL);
  series_ = actionArgs.hasKey("series");
  avgout_ = actionArgs.GetStringKey("avgout");
  solvout_ = actionArgs.GetStringKey("solvout");
  bridgeout_ = actionArgs.GetStringKey("bridgeout");
  acut_ = actionArgs.getKeyDouble("angle",135.0);
  // Convert angle cutoff to radians
  acut_ *= DEGRAD;
  double dcut = actionArgs.getKeyDouble("dist",3.0);
  dcut2_ = dcut * dcut;
  // Get donor mask
  char* mask = actionArgs.getKeyString("donormask",NULL);
  if (mask!=NULL) {
    DonorMask_.SetMaskString(mask);
    hasDonorMask_=true;
  }
  // Get acceptor mask
  mask = actionArgs.getKeyString("acceptormask",NULL);
  if (mask!=NULL) {
    AcceptorMask_.SetMaskString(mask);
    hasAcceptorMask_=true;
  }
  // Get solvent donor mask
  mask = actionArgs.getKeyString("solventdonor", NULL);
  if (mask!=NULL) {
    SolventDonorMask_.SetMaskString(mask);
    hasSolventDonor_ = true;
    calcSolvent_ = true;
  }
  // Get solvent acceptor mask
  mask = actionArgs.getKeyString("solventacceptor", NULL);
  if (mask!=NULL) {
    SolventAcceptorMask_.SetMaskString(mask);
    hasSolventAcceptor_ = true;
    calcSolvent_ = true;
  }
  // Get generic mask
  mask = actionArgs.getNextMask();
  Mask_.SetMaskString(mask);

  // Setup datasets
  hbsetname_ = actionArgs.GetStringNext();
  NumHbonds_ = DSL->AddSet(DataSet::INT, hbsetname_, "HB");
  if (NumHbonds_==NULL) return 1;
  NumHbonds_->SetAspect("UU");
  DFL->Add(outfilename,NumHbonds_);
  if (calcSolvent_) {
    NumSolvent_ = DSL->AddSetAspect(DataSet::INT, NumHbonds_->Name(), "UV");
    if (NumSolvent_ == NULL) return 1;
    DFL->Add(outfilename, NumSolvent_);
    NumBridge_ = DSL->AddSetAspect(DataSet::INT, NumHbonds_->Name(), "Bridge");
    if (NumBridge_ == NULL) return 1;
    DFL->Add(outfilename, NumBridge_);
  } 

  mprintf( "  HBOND: ");
  if (!hasDonorMask_ && !hasAcceptorMask_)
    mprintf("Searching for Hbond donors/acceptors in region specified by %s\n",
            Mask_.MaskString());
  else if (hasDonorMask_ && !hasAcceptorMask_)
    mprintf("Donor mask is %s, acceptors will be searched for in region specified by %s\n",
            DonorMask_.MaskString(), Mask_.MaskString());
  else if (hasAcceptorMask_ && !hasDonorMask_)
    mprintf("Acceptor mask is %s, donors will be searched for in a region specified by %s\n",
            AcceptorMask_.MaskString(), Mask_.MaskString());
  else
    mprintf("Donor mask is %s, Acceptor mask is %s\n",
            DonorMask_.MaskString(), AcceptorMask_.MaskString());
  if (hasSolventDonor_)
    mprintf("         Will search for hbonds between solute and solvent donors in [%s]\n",
            SolventDonorMask_.MaskString());
  if (hasSolventAcceptor_)
    mprintf("         Will search for hbonds between solute and solvent acceptors in [%s]\n",
            SolventAcceptorMask_.MaskString());
  mprintf( "         Distance cutoff = %.3lf, Angle Cutoff = %.3lf\n",dcut,acut_*RADDEG);
  if (outfilename!=NULL) 
    mprintf( "         Dumping # Hbond v time results to %s\n", outfilename);
  if (!avgout_.empty())
    mprintf( "         Dumping Hbond avgs to %s\n",avgout_.c_str());
  if (calcSolvent_ && !solvout_.empty())
    mprintf("          Dumping solute-solvent hbond avgs to %s\n", solvout_.c_str());
  if (calcSolvent_ && !bridgeout_.empty())
    mprintf("          Dumping solvent bridging info to %s\n", bridgeout_.c_str());
  if (series_)
    mprintf("          Time series data for each hbond will be saved for analysis.\n");

  return 0;
}

// Action_Hbond::SearchAcceptor()
/** Search for hbond acceptors X in the region specified by amask.
  * If Auto is true select acceptors based on the rule that "Hydrogen 
  * bonds are FON"
  */
void Action_Hbond::SearchAcceptor(HBlistType& alist, AtomMask& amask, bool Auto) {
  bool isAcceptor;
  // Set up acceptors: F, O, N
  // NOTE: Attempt to determine electronegative carbons?
  for (AtomMask::const_iterator atom = amask.begin();
                                atom != amask.end(); ++atom)
  {
    isAcceptor=true;
    // If auto searching, only consider acceptor atoms as F, O, N
    if (Auto) {
      isAcceptor=false;
      if ( (*currentParm)[*atom].Element() == Atom::FLUORINE ||
           (*currentParm)[*atom].Element() == Atom::OXYGEN ||
           (*currentParm)[*atom].Element() == Atom::NITROGEN    )
       isAcceptor=true;
    }
    if (isAcceptor)
      alist.push_back(*atom);
  }
}

// Action_Hbond::SearchDonor()
/** Search for hydrogen bond donors X-H in the region specified by dmask.
  * If Auto is true select donors based on the rule that "Hydrogen bonds 
  * are FON"
  */
void Action_Hbond::SearchDonor(HBlistType& dlist, AtomMask& dmask, bool Auto) {
  bool isDonor;
  // Set up donors: F-H, O-H, N-H
  for (AtomMask::const_iterator donoratom = dmask.begin();
                                donoratom != dmask.end(); ++donoratom)
  {
    // If this is already an H atom continue
    if ( (*currentParm)[*donoratom].Element() == Atom::HYDROGEN ) continue;
    isDonor = true;
    // If auto searching, only consider donor atoms as F, O, N
    if (Auto) {
      isDonor=false;
      if ( (*currentParm)[*donoratom].Element() == Atom::FLUORINE ||
           (*currentParm)[*donoratom].Element() == Atom::OXYGEN ||
           (*currentParm)[*donoratom].Element() == Atom::NITROGEN   )
        isDonor=true;
    }
    if (isDonor) {
      // If no bonds to this atom assume it is an ion. Only do this if !Auto
      if (!Auto && (*currentParm)[*donoratom].Nbonds() == 0) {
        dlist.push_back(*donoratom);
        dlist.push_back(*donoratom);
      } else {
        // Get list of hydrogen atoms bonded to this atom
        for (Atom::bond_iterator batom = (*currentParm)[*donoratom].bondbegin();
                                 batom != (*currentParm)[*donoratom].bondend();
                                 batom++)
        {
          if ( (*currentParm)[*batom].Element() == Atom::HYDROGEN ) {
            //mprintf("BOND TO H: %i@%s -- %i@%s\n",*donoratom+1,(*currentParm)[*donoratom].c_str(),
            //        *batom+1,(*currentParm)[*batom].c_str());
            dlist.push_back(*donoratom);
            dlist.push_back(*batom);
          }
        }
      }
    } // END atom is potential donor
  } // END loop over selected atoms
}

// Action_Hbond::setup()
/** Search for hbond donors and acceptors. */
int Action_Hbond::setup() {
  // Set up mask
  if (!hasDonorMask_ || !hasAcceptorMask_) {
    if ( currentParm->SetupIntegerMask( Mask_ ) ) return 1;
    if ( Mask_.None() ) {
      mprintf("Warning: Hbond::setup: Mask has no atoms.\n");
      return 1;
    }
  }
  // Set up donor mask
  if (hasDonorMask_) {
    if ( currentParm->SetupIntegerMask( DonorMask_ ) ) return 1;
    if (DonorMask_.None()) {
      mprintf("Warning: Hbond: DonorMask has no atoms.\n");
      return 1;
    }
  }
  // Set up acceptor mask
  if (hasAcceptorMask_) {
    if ( currentParm->SetupIntegerMask( AcceptorMask_ ) ) return 1;
    if (AcceptorMask_.None()) {
      mprintf("Warning: Hbond: AcceptorMask has no atoms.\n");
      return 1;
    }
  }
  // Set up solvent donor/acceptor
  if (hasSolventDonor_) {
    if (currentParm->SetupIntegerMask( SolventDonorMask_ )) return 1;
    if (SolventDonorMask_.None()) {
      mprintf("Warning: Hbond: SolventDonorMask has no atoms.\n");
      return 1;
    }
  }
  if (hasSolventAcceptor_) {
    if (currentParm->SetupIntegerMask( SolventAcceptorMask_ )) return 1;
    if (SolventAcceptorMask_.None()) {
      mprintf("Warning: Hbond: SolventAcceptorMask has no atoms.\n");
      return 1;
    }
  }

  // OK TO CLEAR?
  Acceptor_.clear();
  Donor_.clear();
  // SOLUTE: Four cases:
  // 1) DonorMask and AcceptorMask NULL: donors and acceptors automatically searched for.
  if (!hasDonorMask_ && !hasAcceptorMask_) {
    SearchAcceptor(Acceptor_, Mask_,true);
    SearchDonor(Donor_, Mask_,true);
  
  // 2) DonorMask only: acceptors automatically searched for in Mask
  } else if (hasDonorMask_ && !hasAcceptorMask_) {
    SearchAcceptor(Acceptor_, Mask_,true);
    SearchDonor(Donor_, DonorMask_, false);

  // 3) AcceptorMask only: donors automatically searched for in Mask
  } else if (!hasDonorMask_ && hasAcceptorMask_) {
    SearchAcceptor(Acceptor_, AcceptorMask_, false);
    SearchDonor(Donor_, Mask_,true);

  // 4) Both DonorMask and AcceptorMask: No automatic search.
  } else {
    SearchAcceptor(Acceptor_, AcceptorMask_, false);
    SearchDonor(Donor_, DonorMask_, false);
  }

  // Print acceptor/donor information
  mprintf("\tSet up %zu acceptors:\n", Acceptor_.size() );
  if (debug>0) {
    for (HBlistType::iterator accept = Acceptor_.begin(); accept!=Acceptor_.end(); accept++)
      mprintf("        %8i: %4s\n",*accept+1,(*currentParm)[*accept].c_str());
  }
  mprintf("\tSet up %zu donors:\n", Donor_.size()/2 );
  if (debug>0) {
    for (HBlistType::iterator donor = Donor_.begin(); donor!=Donor_.end(); donor++) {
      int atom = (*donor);
      ++donor;
      int a2   = (*donor);
      mprintf("        %8i:%4s - %8i:%4s\n",atom+1,(*currentParm)[atom].c_str(),
              a2+1,(*currentParm)[a2].c_str()); 
    } 
  }

  // SOLVENT:
  if (hasSolventAcceptor_) {
    SolventAcceptor_.clear();
    SearchAcceptor(SolventAcceptor_, SolventAcceptorMask_, false);
    mprintf("\tSet up %zu solvent acceptors\n", SolventAcceptor_.size() );
  }
  if (hasSolventDonor_) {
    SolventDonor_.clear();
    SearchDonor(SolventDonor_, SolventDonorMask_, false);
    mprintf("\tSet up %zu solvent donors\n", SolventDonor_.size()/2 );
  }


  return 0;
}

// Action_Hbond::AtomsAreHbonded()
/** Used to determine if solute atoms are bonded to solvent atoms. */
int Action_Hbond::AtomsAreHbonded(int a_atom, int d_atom, int h_atom, int hbidx, bool solutedonor) 
{
  std::string hblegend;
  HbondType HB;
  double angle;

  if (a_atom == d_atom) return 0;
  double dist2 = currentFrame->DIST2(a_atom, d_atom);
  if (dist2 > dcut2_) return 0;
  /*mprintf("DEBUG: Donor %i@%s -- acceptor %i@%s = %lf",
         d_atom+1, (*currentParm)[d_atom].c_str(),
         a_atom+1, (*currentParm)[a_atom].c_str(), sqrt(dist2));*/
  // For ions, donor atom will be same as h atom so no angle needed.
  if (d_atom != h_atom) {
    angle = currentFrame->ANGLE(a_atom, h_atom, d_atom);
    if (angle < acut_) return 0;
  }
  double dist = sqrt(dist2);
  //mprintf( "A-D HBOND[%6i]: %6i@%-4s ... %6i@%-4s-%6i@%-4s Dst=%6.2lf Ang=%6.2lf\n", hbidx, 
  //        a_atom, (*currentParm)[a_atom].c_str(),
  //        h_atom, (*currentParm)[h_atom].c_str(), 
  //        d_atom, (*currentParm)[d_atom].c_str(), dist, angle*RADDEG);
  // Find hbond in map
  HBmapType::iterator entry = SolventMap_.find( hbidx );
  if (entry == SolventMap_.end() ) {
    // New Hbond
    if (solutedonor) {
      HB.A = -1; // Do not care about which solvent acceptor
      HB.D = d_atom;
      HB.H = h_atom;
      hblegend = currentParm->TruncResAtomName(d_atom) + "-V";
    } else {
      HB.A = a_atom;
      HB.D = -1; // Do not care about solvent donor heavy atom
      HB.H = -1; // Do not care about solvent donor H atom
      hblegend = currentParm->TruncResAtomName(a_atom) + "-V";
    }
    HB.Frames = 1;
    HB.dist = dist;
    HB.angle = angle;
    if (series_) {
      HB.data_ = (DataSet_integer*) DSL->AddSetIdxAspect( DataSet::INT, NumHbonds_->Name(), 
                                                          hbidx, "solventhb" );
      //mprinterr("Created Solvent HB data frame %i idx %i %p\n",frameNum,hbidx,HB.data_);
      HB.data_->Resize( DSL->MaxFrames() );
      HB.data_->SetLegend( hblegend );
      (*HB.data_)[ frameNum ] = 1;
    }
    SolventMap_.insert( entry, std::pair<int,HbondType>(hbidx, HB) );
  } else {
    (*entry).second.Frames++;
    (*entry).second.dist += dist;
    (*entry).second.angle += angle;
    if (series_) {
      //mprinterr("Adding Solvent HB data frame %i idx %i %p\n",frameNum,hbidx,(*entry).second.data_);
      (*(*entry).second.data_)[ frameNum ] = 1;
    }
  }     
  return 1;
}

// Action_Hbond::action()
/** Calculate distance between all donors and acceptors. Store Hbond info.
  */    
int Action_Hbond::action() {
  // accept ... H-D
  int D, H;
  double dist, dist2, angle;//, ucell[9], recip[9];
  HBmapType::iterator it;
  HbondType HB;

  // SOLUTE-SOLUTE HBONDS
  int hbidx = 0; 
  int numHB=0;
  for (HBlistType::iterator donor = Donor_.begin(); donor!=Donor_.end(); ++donor) {
    D = (*donor);
    ++donor;
    H = (*donor);
    for (HBlistType::iterator accept = Acceptor_.begin(); 
                              accept != Acceptor_.end(); ++accept, ++hbidx) 
    {
      if (*accept == D) continue;
      dist2 = currentFrame->DIST2(*accept, D);
      //dist2 = currentFrame->DIST2(*accept, D, (int)P->boxType, ucell, recip);
      if (dist2 > dcut2_) continue;
      angle = currentFrame->ANGLE(*accept, H, D);
      if (angle < acut_) continue;
//      mprintf( "HBOND[%i]: %i:%s ... %i:%s-%i:%s Dist=%lf Angle=%lf\n", 
//              hbidx, *accept, P->names[*accept],
//              H, P->names[H], D, P->names[D], dist, angle);
      ++numHB;
      dist = sqrt(dist2);
      // Find hbond in map
      it = HbondMap_.find( hbidx );
      if (it == HbondMap_.end() ) {
        // New Hbond
        HB.A = *accept;
        HB.D = D;
        HB.H = H;
        HB.Frames = 1;
        HB.dist = dist;
        HB.angle = angle;
        if (series_) {
          std::string hblegend = currentParm->TruncResAtomName(*accept) + "-" +
                                 currentParm->TruncResAtomName(D);
          HB.data_ = (DataSet_integer*) DSL->AddSetIdxAspect( DataSet::INT, NumHbonds_->Name(),
                                                              hbidx, "solutehb" );
          //mprinterr("Created solute Hbond dataset index %i\n", hbidx);
          HB.data_->Resize( DSL->MaxFrames() );
          HB.data_->SetLegend( hblegend );
          (*HB.data_)[ frameNum ] = 1 ;
        }
        HbondMap_.insert( it, std::pair<int,HbondType>(hbidx, HB) );
      } else {
        (*it).second.Frames++;
        (*it).second.dist += dist;
        (*it).second.angle += angle;
        if (series_)
          (*(*it).second.data_)[ frameNum ] = 1;
      }
    }
  }
  NumHbonds_->Add(frameNum, &numHB);
  //mprintf("HBOND: Scanned %i hbonds.\n",hbidx);
  
  if (calcSolvent_) {
    // Contains info about which residue(s) a Hbonding solvent mol is
    // Hbonded to.
    std::map< int, std::set<int> > solvent2solute;
    int solventHbonds = 0;
    // SOLUTE DONOR-SOLVENT ACCEPTOR
    // Index by solute H atom. 
    if (hasSolventAcceptor_) {
      numHB = 0;
      for (HBlistType::iterator donor = Donor_.begin(); 
                                donor != Donor_.end(); ++donor) 
      {
        D = (*donor);
        ++donor;
        H = (*donor);
        for (HBlistType::iterator accept = SolventAcceptor_.begin(); 
                                  accept != SolventAcceptor_.end(); ++accept)
        { 
          if (AtomsAreHbonded( *accept, D, H, H, true )) {
            ++numHB;
            int soluteres = (*currentParm)[D].ResNum();
            int solventmol = (*currentParm)[*accept].Mol();
            solvent2solute[solventmol].insert( soluteres );
            //mprintf("DBG:\t\tSolvent Mol %i bonded to solute res %i\n",solventmol+1,soluteres+1);
          }
        }
      }
      //mprintf("DEBUG: # Solvent Acceptor to Solute Donor Hbonds is %i\n", numHB);
      solventHbonds += numHB;
    }
    // SOLVENT DONOR-SOLUTE ACCEPTOR
    // Index by solute acceptor atom
    if (hasSolventDonor_) {
      numHB = 0;
      for (HBlistType::iterator donor = SolventDonor_.begin();
                                donor != SolventDonor_.end(); ++donor)
      {
        D = (*donor);
        ++donor;
        H = (*donor);
        for (HBlistType::iterator accept = Acceptor_.begin();
                                  accept != Acceptor_.end(); ++accept)
        {
          if (AtomsAreHbonded( *accept, D, H, *accept, false )) {
            ++numHB;
            int soluteres = (*currentParm)[*accept].ResNum();
            int solventmol = (*currentParm)[D].Mol();
            solvent2solute[solventmol].insert( soluteres );
            //mprintf("DBG:\t\tSolvent Mol %i bonded to solute res %i\n",solventmol+1,soluteres+1);
          }
        }
      }
      //mprintf("DEBUG: # Solvent Donor to Solute Acceptor Hbonds is %i\n", numHB);
      solventHbonds += numHB;
    }
    NumSolvent_->Add(frameNum, &solventHbonds);

    // Determine number of bridging waters.
    numHB = 0;
    for (std::map< int, std::set<int> >::iterator bridge = solvent2solute.begin();
                                                  bridge != solvent2solute.end();
                                                  ++bridge)
    {
      // If solvent molecule is bound to 2 or more different residues,
      // it is bridging. 
      if ( (*bridge).second.size() > 1) {
        ++numHB;
        //mprintf("DBG:\t\tSolvent mol %i is bridging residues", (*bridge).first+1);
        //for (std::set<int>::iterator res = (*bridge).second.begin();
        //                             res != (*bridge).second.end(); ++res) 
        //  mprintf(" %i", *res+1);
        //mprintf("\n");
        // Find bridge in map based on this combo of residues (bridge.second)
        BridgeType::iterator b_it = BridgeMap_.find( (*bridge).second );
        if (b_it == BridgeMap_.end() ) // New Bridge 
          BridgeMap_.insert( b_it, std::pair<std::set<int>,int>((*bridge).second, 1) );
        else                           // Increment bridge #frames
          (*b_it).second++;
      }
    }
    NumBridge_->Add(frameNum, &numHB);
  }

  ++Nframes_;

  return 0;
}

// Action_Hbond::print()
/** Print average occupancies over all frames for all detected Hbonds
  */
void Action_Hbond::print() {
  std::vector<HbondType> HbondList; // For sorting
  std::string Aname, Hname, Dname;
  CpptrajFile outfile;

  // Calculate necessary column width for strings based on how many residues.
  // ResName+'_'+ResNum+'@'+AtomName | NUM = 4+1+R+1+4 = R+10
  int NUM = DigitWidth( currentParm->Nres() ) + 10;

  // Solute Hbonds 
  if (!avgout_.empty()) { 
    if (outfile.OpenWrite( avgout_ )) return;
    // Place all detected Hbonds in a list and sort. Free memory as we go. 
    for (HBmapType::iterator it = HbondMap_.begin(); it!=HbondMap_.end(); ++it) 
      HbondList.push_back( (*it).second );
    HbondMap_.clear();
    sort( HbondList.begin(), HbondList.end(), hbond_cmp() );
    // Calculate averages and print
    //outfile.Printf("#Solute Hbonds:\n");
    outfile.Printf("%-*s %*s %*s %8s %12s %12s %12s\n", NUM, "#Acceptor", 
                   NUM, "DonorH", NUM, "Donor", "Frames", "Frac", "AvgDist", "AvgAng");
    for (std::vector<HbondType>::iterator hbond = HbondList.begin(); 
                                          hbond!=HbondList.end(); ++hbond ) 
    {
      double avg = (double) (*hbond).Frames;
      avg = avg / ((double) Nframes_);
      double dist = (double) (*hbond).dist;
      dist = dist / ((double) (*hbond).Frames);
      double angle = (double) (*hbond).angle;
      angle = angle / ((double) (*hbond).Frames);
      angle *= RADDEG;

      Aname = currentParm->ResAtomName((*hbond).A);
      Hname = currentParm->ResAtomName((*hbond).H);
      Dname = currentParm->ResAtomName((*hbond).D);

      outfile.Printf("%-*s %*s %*s %8i %12.4lf %12.4lf %12.4lf\n",
                     NUM, Aname.c_str(), NUM, Hname.c_str(), NUM, Dname.c_str(),
                     (*hbond).Frames, avg, dist, angle);
    }
    outfile.CloseFile();
  }

  // Solute-solvent Hbonds 
  if (!solvout_.empty() && calcSolvent_) {
    if (solvout_ == avgout_) {
      if (outfile.OpenAppend( solvout_ )) return;
    } else {
      if (outfile.OpenWrite( solvout_)) return;
    }
    HbondList.clear();
    for (HBmapType::iterator it = SolventMap_.begin(); it != SolventMap_.end(); ++it)
      HbondList.push_back( (*it).second );
    SolventMap_.clear();
    sort( HbondList.begin(), HbondList.end(), hbond_cmp() );
    // Calc averages and print
    outfile.Printf("#Solute-Solvent Hbonds:\n");
    outfile.Printf("%-*s %*s %*s %8s %12s %12s %12s\n", NUM, "#Acceptor", 
                   NUM, "DonorH", NUM, "Donor", "Count", "Frac", "AvgDist", "AvgAng");
    for (std::vector<HbondType>::iterator hbond = HbondList.begin();
                                          hbond != HbondList.end(); ++hbond )
    {
      // Average has slightly diff meaning since for any given frame multiple
      // solvent can bond to the same solute.
      double avg = (double) (*hbond).Frames;
      avg = avg / ((double) Nframes_);
      double dist = (double) (*hbond).dist;
      dist = dist / ((double) (*hbond).Frames);
      double angle = (double) (*hbond).angle;
      angle = angle / ((double) (*hbond).Frames);
      angle *= RADDEG;

      if ((*hbond).A==-1) // Solvent acceptor
        Aname = "SolventAcc";
      else
        Aname = currentParm->ResAtomName((*hbond).A);
      if ((*hbond).D==-1) { // Solvent donor
        Dname = "SolventDnr";
        Hname = "SolventH  ";
      } else {
        Dname = currentParm->ResAtomName((*hbond).D);
        Hname = currentParm->ResAtomName((*hbond).H);
      }

      outfile.Printf("%-*s %*s %*s %8i %12.4lf %12.4lf %12.4lf\n",
                     NUM, Aname.c_str(), NUM, Hname.c_str(), NUM, Dname.c_str(),
                     (*hbond).Frames, avg, dist, angle);
    }
    outfile.CloseFile();
  }

  // BRIDGING INFO
  if (!bridgeout_.empty() && calcSolvent_) {
    if (bridgeout_ == avgout_ || bridgeout_ == solvout_) {
      if (outfile.OpenAppend( bridgeout_ )) return;
    } else {
      if (outfile.OpenWrite( bridgeout_ )) return; 
    }
    outfile.Printf("#Bridging Solute Residues:\n");
    for (BridgeType::iterator it = BridgeMap_.begin(); 
                              it != BridgeMap_.end(); ++it) 
    {
      outfile.Printf("Bridge Res");
      for (std::set<int>::iterator res = (*it).first.begin();
                                   res != (*it).first.end(); ++res)
        outfile.Printf(" %i:%s", *res+1, currentParm->Res( *res ).c_str());
      outfile.Printf(", %i frames.\n", (*it).second);
    }
    outfile.CloseFile();
  } 
}

