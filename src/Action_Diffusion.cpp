#include <cmath> // sqrt
#include "Action_Diffusion.h"
#include "CpptrajStdio.h"

// CONSTRUCTOR
Action_Diffusion::Action_Diffusion() :
  printIndividual_(false),
  time_(1),
  hasBox_(false)
{}

/** diffusion mask [average] [time <time per frame>] */
int Action_Diffusion::init() {
  printIndividual_ = !(actionArgs.hasKey("average"));
  mask_.SetMaskString( actionArgs.getNextMask() );
  time_ = actionArgs.getNextDouble(1.0);
  if (time_ < 0) {
    mprinterr("Error: diffusion: time per frame incorrectly specified\n");
    return 1;
  }
  std::string outputNameRoot = actionArgs.GetStringNext();
  // Default filename: 'diffusion'
  if (outputNameRoot.empty()) 
    outputNameRoot.assign("diffusion");
  
  // Open output files
  std::string fname = outputNameRoot + "_x.xmgr";
  if (outputx_.OpenWrite( fname )) return 1;
  fname = outputNameRoot + "_y.xmgr";
  if (outputy_.OpenWrite( fname )) return 1;
  fname = outputNameRoot + "_z.xmgr";
  if (outputz_.OpenWrite( fname )) return 1;
  fname = outputNameRoot + "_r.xmgr";
  if (outputr_.OpenWrite( fname )) return 1;
  fname = outputNameRoot + "_a.xmgr";
  if (outputa_.OpenWrite( fname )) return 1;

  mprintf("    DIFFUSION:\n");
  if (printIndividual_)
    mprintf("\tThe average and individual results will ");
  else
    mprintf("\tOnly the average results will ");
  mprintf("be printed to %s_?.xmgr\n", outputNameRoot.c_str());
  mprintf("\tThe time between frames in psec is %5.3f.\n", time_);
  mprintf("\tTo calculated diffusion constants, calculate the slope of the lines(s)\n");
  mprintf("\tand multiply by 10.0/6.0; this will give units of 1x10**-5 cm**2/s\n");
  mprintf("\tAtom Mask is [%s]\n", mask_.MaskString());

  return 0;
}

int Action_Diffusion::setup() {
  // Setup atom mask
  if (currentParm->SetupIntegerMask( mask_ )) return 1;
  if (mask_.None()) {
    mprinterr("Error: diffusion: No atoms selected.\n");
    return 1;
  }

  // Check for box
  if ( currentParm->BoxType()!=Box::NOBOX )
    hasBox_ = true;
  else
    hasBox_ = false;

  // Allocate the distance arrays
  distancex_.resize( mask_.Nselected() );
  distancey_.resize( mask_.Nselected() );
  distancez_.resize( mask_.Nselected() );
  distance_.resize(  mask_.Nselected() );

  // Allocate the delta arrays
  deltax_.assign( mask_.Nselected(), 0 );
  deltay_.assign( mask_.Nselected(), 0 );
  deltaz_.assign( mask_.Nselected(), 0 );

  // Reserve space for the initial and previous frame arrays
  //initial_.reserve( mask_.Nselected() );
  previousx_.reserve( mask_.Nselected() );
  previousy_.reserve( mask_.Nselected() );
  previousz_.reserve( mask_.Nselected() );

  // If initial frame already set and current # atoms > # atoms in initial
  // frame this will probably cause an error.
  if (!initial_.empty() && currentParm->Natom() > initial_.Natom()) {
    mprintf("Warning: # atoms in current parm (%s, %i) > # atoms in initial frame (%i)\n",
             currentParm->c_str(), currentParm->Natom(), initial_.Natom());
    mprintf("Warning: This may lead to segmentation faults.\n");
  }

  return 0;
}

int Action_Diffusion::action() {
  double XYZ[3], boxcenter[3], boxcrd[3], iXYZ[3];
  // Load initial frame if necessary
  if (initial_.empty()) {
    initial_ = *currentFrame;
    for (AtomMask::const_iterator atom = mask_.begin(); atom != mask_.end(); ++atom)
    {
      currentFrame->GetAtomXYZ(XYZ, *atom);
      //initial_.push_back( XYZ[0] );
      previousx_.push_back( XYZ[0] );
      //initial_.push_back( XYZ[1] );
      previousy_.push_back( XYZ[1] );
      //initial_.push_back( XYZ[2] );
      previousz_.push_back( XYZ[2] );
    }
  } else {
    if (hasBox_) {
      currentFrame->BoxXYZ(boxcrd);
      boxcenter[0] = boxcrd[0] / 2;
      boxcenter[1] = boxcrd[1] / 2;
      boxcenter[2] = boxcrd[2] / 2;
    }
    // Set iterators
    std::vector<double>::iterator px = previousx_.begin();
    std::vector<double>::iterator py = previousy_.begin();
    std::vector<double>::iterator pz = previousz_.begin();
    std::vector<double>::iterator dx = deltax_.begin();
    std::vector<double>::iterator dy = deltay_.begin();
    std::vector<double>::iterator dz = deltaz_.begin();
    std::vector<double>::iterator distx = distancex_.begin();
    std::vector<double>::iterator disty = distancey_.begin();
    std::vector<double>::iterator distz = distancez_.begin();
    std::vector<double>::iterator dist  = distance_.begin();
    // For averaging over selected atoms
    double average = 0;
    double avgx = 0;
    double avgy = 0;
    double avgz = 0;
    for (AtomMask::const_iterator atom = mask_.begin(); atom != mask_.end(); ++atom)
    { // Get current and initial coords for this atom.
      currentFrame->GetAtomXYZ(XYZ, *atom);
      initial_.GetAtomXYZ(iXYZ, *atom);
      // Calculate distance to previous frames coordinates.
      double delx = XYZ[0] - *px;
      double dely = XYZ[1] - *py;
      double delz = XYZ[2] - *pz;
      // If the particle moved more than half the box, assume
      // it was imaged and adjust the distance of the total
      // movement with respect to the original frame.
      if (hasBox_) {
        if      (delx >  boxcenter[0]) *dx -= boxcrd[0];
        else if (delx < -boxcenter[0]) *dx += boxcrd[0];
        else if (dely >  boxcenter[1]) *dy -= boxcrd[1];
        else if (dely < -boxcenter[1]) *dy += boxcrd[1];
        else if (delz >  boxcenter[2]) *dz -= boxcrd[2];
        else if (delz < -boxcenter[2]) *dz += boxcrd[2];
      }
      // DEBUG
      if (debug > 2)
        mprintf("ATOM: %5i %10.3f %10.3f %10.3f",*atom,XYZ[0],delx,*dx);
      // Set the current x with reference to the un-imaged trajectory.
      double xx = XYZ[0] + *dx; 
      double yy = XYZ[1] + *dy; 
      double zz = XYZ[2] + *dz;
      // Calculate the distance between this "fixed" coordinate
      // and the reference (initial) frame.
      delx = xx - iXYZ[0];
      dely = yy - iXYZ[1];
      delz = zz - iXYZ[2];
      // DEBUG
      if (debug > 2)
        mprintf(" %10.3f\n", delx);
      // Store distance for this atom
      *distx = delx*delx;
      *disty = dely*dely;
      *distz = delz*delz;
      *dist = *distx + *disty + *distz;
      // Accumulate averages
      avgx += *distx;
      avgy += *disty;
      avgz += *distz;
      average += *dist;
      // Update the previous coordinate set to match the current coordinates
      *px = XYZ[0];
      *py = XYZ[1];
      *pz = XYZ[2];
      // Increment iterators
      ++px;
      ++py;
      ++pz;
      ++dx;
      ++dy;
      ++dz;
      ++distx;
      ++disty;
      ++distz;
      ++dist;
    } // END loop over selected atoms
    double dNselected = (double)mask_.Nselected();
    average /= dNselected;
    avgx /= dNselected;
    avgy /= dNselected;
    avgz /= dNselected;

    // ----- OUTPUT -----
    // Output averages
    double Time = time_ * (double)frameNum;
    outputx_.Printf("%8.3f  %8.3f", Time, avgx);
    outputy_.Printf("%8.3f  %8.3f", Time, avgy);
    outputz_.Printf("%8.3f  %8.3f", Time, avgz);
    outputr_.Printf("%8.3f  %8.3f", Time, average);
    outputa_.Printf("%8.3f  %8.3f", Time, sqrt(average));
    // Individual values
    if (printIndividual_) {
      for (int i = 0; i < mask_.Nselected(); ++i) {
        outputx_.Printf("  %8.3f", distancex_[i]);
        outputy_.Printf("  %8.3f", distancey_[i]);
        outputz_.Printf("  %8.3f", distancez_[i]);
        outputr_.Printf("  %8.3f", distance_[i]);
        outputa_.Printf("  %8.3f", sqrt(distance_[i]));
      }
    }
    // Print newlines
    outputx_.Printf("\n");
    outputy_.Printf("\n");
    outputz_.Printf("\n");
    outputr_.Printf("\n");
    outputa_.Printf("\n");
  }

  return 0;
}  