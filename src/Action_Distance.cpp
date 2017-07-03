#include <cmath>
#include "Action_Distance.h"
#include "CpptrajStdio.h"

// CONSTRUCTOR
Action_Distance::Action_Distance() :
  dist_(0),
  mode_(NORMAL),
  plane_(XY),
  useMass_(true)
{}

// Action_Distance::Help()
void Action_Distance::Help() const {
  mprintf("\t[<name>] <mask1> <mask2> [out <filename>] [geom] [noimage] [type noe]\n"
          "\tOptions for 'type noe':\n"
          "\t  %s\n"
          "  Calculate distance between atoms in <mask1> and <mask2>\n", 
          AssociatedData_NOE::HelpText);
}

// Action_Distance::Init()
Action::RetType Action_Distance::Init(ArgList& actionArgs, ActionInit& init, int debugIn)
{
  AssociatedData_NOE noe;
  // Get Keywords
  image_.InitImaging( !(actionArgs.hasKey("noimage")) );
  useMass_ = !(actionArgs.hasKey("geom"));
  DataFile* outfile = init.DFL().AddDataFile( actionArgs.GetStringKey("out"), actionArgs );
  MetaData::scalarType stype = MetaData::UNDEFINED;
  std::string stypename = actionArgs.GetStringKey("type");
  if ( stypename == "noe" ) {
    stype = MetaData::NOE;
    if (noe.NOE_Args( actionArgs )) return Action::ERR;
  }
  // Determine mode.
  ReferenceFrame refFrm = init.DSL().GetReferenceFrame( actionArgs );
  if (refFrm.error()) return Action::ERR;
  mode_ = NORMAL;
  if (!refFrm.empty())
    mode_ = REF;
  else {
    std::string pstr = actionArgs.GetStringKey("plane");
    if (!pstr.empty()) {
      mode_ = PLANE;
      if (pstr == "xy")
        plane_ = XY;
      else if (pstr == "yz")
        plane_ = YZ;
      else if (pstr == "xz")
        plane_ = XZ;
      else {
        mprinterr("Error: Unrecognized argument for 'plane' (%s)\n", pstr.c_str());
        return Action::ERR;
      }
    }
  }

  // Get Masks
  std::string maskexp = actionArgs.GetMaskNext();
  if (maskexp.empty()) {
    mprinterr("Error: Need at least 1 atom mask.\n");
    return Action::ERR;
  }
  Mask1_.SetMaskString(maskexp);
  if (mode_ != PLANE) {
    maskexp = actionArgs.GetMaskNext();
    if (maskexp.empty()) {
      mprinterr("Error: Need 2 atom masks.\n");
      return Action::ERR;
    }
    Mask2_.SetMaskString(maskexp);
  }

  // Set up reference and get reference point
  if (mode_ == REF) {
    if (refFrm.Parm().SetupIntegerMask( Mask2_, refFrm.Coord() ))
      return Action::ERR;
    if (useMass_)
      refCenter_ = refFrm.Coord().VCenterOfMass( Mask2_ );
    else
      refCenter_ = refFrm.Coord().VGeometricCenter( Mask2_ );
  }

  // Dataset to store distances TODO store masks in data set?
  dist_ = init.DSL().AddSet(DataSet::DOUBLE, MetaData(actionArgs.GetStringNext(),
                                                MetaData::M_DISTANCE, stype), "Dis");
  if (dist_==0) return Action::ERR;
  if ( stype == MetaData::NOE ) {
    dist_->AssociateData( &noe );
    dist_->SetLegend(Mask1_.MaskExpression() + " and " + Mask2_.MaskExpression());
  }
  // Add dataset to data file
  if (outfile != 0) outfile->AddDataSet( dist_ );

  mprintf("    DISTANCE:");
  if (mode_ == NORMAL)
    mprintf(" %s to %s", Mask1_.MaskString(), Mask2_.MaskString());
  else if (mode_ == REF)
    mprintf(" %s to %s (%i atoms) in %s", Mask1_.MaskString(),
            Mask2_.MaskString(), Mask2_.Nselected(), refFrm.refName());
  else if (mode_ == PLANE) {
    static const char* PLANE[3] = { "YZ", "XZ", "XY" };
    mprintf(" %s to the %s plane", Mask1_.MaskString(), PLANE[plane_]);
  }
  if (!image_.UseImage()) 
    mprintf(", non-imaged");
  if (useMass_) 
    mprintf(", center of mass");
  else
    mprintf(", geometric center");
  mprintf(".\n");

  return Action::OK;
}

// Action_Distance::Setup()
/** Determine what atoms each mask pertains to for the current parm file.
  * Imaging is checked for in Action::Setup. 
  */
Action::RetType Action_Distance::Setup(ActionSetup& setup) {
  if (setup.Top().SetupIntegerMask( Mask1_ )) return Action::ERR;
  if (mode_ == NORMAL) {
    if (setup.Top().SetupIntegerMask( Mask2_ )) return Action::ERR;
    mprintf("\t%s (%i atoms) to %s (%i atoms)",
            Mask1_.MaskString(), Mask1_.Nselected(),
            Mask2_.MaskString(),Mask2_.Nselected());
    if (Mask1_.None() || Mask2_.None()) {
      mprintf("\nWarning: One or both masks have no atoms.\n");
      return Action::SKIP;
    }
  } else {
    mprintf("\t%s (%i atoms)", Mask1_.MaskString(), Mask1_.Nselected());
    if (Mask1_.None()) {
      mprintf("\nWarning: Mask has no atoms.\n");
      return Action::SKIP;
    }
  }
  // Set up imaging info for this parm
  image_.SetupImaging( setup.CoordInfo().TrajBox().Type() );
  if (image_.ImagingEnabled())
    mprintf(", imaged");
  else
    mprintf(", imaging off");
  mprintf(".\n");

  return Action::OK;  
}

// Action_Distance::DoAction()
Action::RetType Action_Distance::DoAction(int frameNum, ActionFrame& frm) {
  double Dist;
  Matrix_3x3 ucell, recip;
  Vec3 a1, a2;

  switch ( mode_ ) {
    case NORMAL:
      if (useMass_) {
        a1 = frm.Frm().VCenterOfMass( Mask1_ );
        a2 = frm.Frm().VCenterOfMass( Mask2_ );
      } else {
        a1 = frm.Frm().VGeometricCenter( Mask1_ );
        a2 = frm.Frm().VGeometricCenter( Mask2_ );
      }
      break;
    case REF:
      if (useMass_)
        a1 = frm.Frm().VCenterOfMass( Mask1_ );
      else
        a1 = frm.Frm().VGeometricCenter( Mask1_ );
      a2 = refCenter_;
      break;
    case PLANE:
      if (useMass_)
        a1 = frm.Frm().VCenterOfMass( Mask1_ );
      else
        a1 = frm.Frm().VGeometricCenter( Mask1_ );
      a1[plane_] = 0.0;
      break;
  }

  switch ( image_.ImageType() ) {
    case NONORTHO:
      frm.Frm().BoxCrd().ToRecip(ucell, recip);
      Dist = DIST2_ImageNonOrtho(a1, a2, ucell, recip);
      break;
    case ORTHO:
      Dist = DIST2_ImageOrtho(a1, a2, frm.Frm().BoxCrd());
      break;
    case NOIMAGE:
      Dist = DIST2_NoImage(a1, a2);
      break;
  }
  Dist = sqrt(Dist);

  dist_->Add(frameNum, &Dist);

  return Action::OK;
}
