#include <cmath> // pow
#include "Analysis_KDE.h"
#include "CpptrajStdio.h"
#include "DataSet_double.h"
#include "DataSet_float.h"
#include "Constants.h" // TWOPI

// CONSTRUCTOR
Analysis_KDE::Analysis_KDE() : 
  data_(0), q_data_(0), bandwidth_(0.0), output_(0), kldiv_(0),
  Kernel_(&Analysis_KDE::GaussianKernel) {}

void Analysis_KDE::Help() {
  mprintf("\t<dataset> [bandwidth <bw>] [out <file>] [name <dsname>]\n"
          "\t[min <min>] [max <max] [step <step>] [bins <bins>]\n"
          "\t[kldiv <dsname2> [klout <outfile>]]\n");
}

// Analysis_KDE::Setup()
Analysis::RetType Analysis_KDE::Setup(ArgList& analyzeArgs, DataSetList* datasetlist,
                            TopologyList* PFLin, DataFileList* DFLin, int debugIn)
{
  Dimension Xdim;
  if (analyzeArgs.Contains("min"))
    Xdim.SetMin( analyzeArgs.getKeyDouble("min", 0.0) );
  if (analyzeArgs.Contains("max"))
    Xdim.SetMax( analyzeArgs.getKeyDouble("max", 0.0) );
  Xdim.SetStep( analyzeArgs.getKeyDouble("step", -1.0) );
  Xdim.SetBins( analyzeArgs.getKeyInt("bins", -1) );
  if (Xdim.Step() < 0.0 && Xdim.Bins() < 0) {
    mprinterr("Error: Must set either bins or step.\n");
    return Analysis::ERR;
  }
  std::string setname = analyzeArgs.GetStringKey("name");
  bandwidth_ = analyzeArgs.getKeyDouble("bandwidth", -1.0);
  DataFile* outfile = DFLin->AddDataFile( analyzeArgs.GetStringKey("out"), analyzeArgs );
  DataFile* klOutfile = 0;
  // Get second data set for KL divergence calc.
  std::string q_dsname = analyzeArgs.GetStringKey("kldiv");
  if (!q_dsname.empty()) {
    q_data_ = datasetlist->GetDataSet( q_dsname );
    if (q_data_ == 0) {
      mprinterr("Error: Data set %s not found.\n", q_dsname.c_str());
      return Analysis::ERR;
    }
    if (q_data_->Ndim() != 1) {
      mprinterr("Error: Only 1D data sets supported.\n");
      return Analysis::ERR;
    }
    klOutfile = DFLin->AddDataFile( analyzeArgs.GetStringKey("klout"), analyzeArgs );
  } else {
    q_data_ = 0;
    kldiv_ = 0;
  }

  // Get data set
  data_ = datasetlist->GetDataSet( analyzeArgs.GetStringNext() );
  if (data_ == 0) {
    mprinterr("Error: No data set or invalid data set name specified\n");
    return Analysis::ERR;
  }
  if (data_->Ndim() != 1) {
    mprinterr("Error: Only 1D data sets supported.\n");
    return Analysis::ERR;
  }
  
  // Output data set
  output_ = datasetlist->AddSet(DataSet::DOUBLE, setname, "kde");
  output_->SetDim(Dimension::X, Xdim);
  if (outfile != 0) outfile->AddSet( output_ );
  // Output for KL divergence calc.
  if ( q_data_ != 0 ) {
    kldiv_ = datasetlist->AddSetAspect(DataSet::FLOAT, output_->Name(), "kld");
    if (klOutfile != 0) klOutfile->AddSet( kldiv_ );
  }

  mprintf("    KDE: Using gaussian KDE to histogram set \"%s\"\n", data_->Legend().c_str());
  if (q_data_ != 0) {
    mprintf("\tCalculating Kullback-Leibler divergence with set \"%s\"\n", 
            q_data_->Legend().c_str());
  }
  if (bandwidth_ < 0.0)
    mprintf("\tBandwidth will be estimated.\n");
  else
    mprintf("\tBandwidth= %f\n", bandwidth_);
  return Analysis::OK;
}

const double Analysis_KDE::ONE_OVER_ROOT_TWOPI = 1.0 / sqrt( TWOPI );

double Analysis_KDE::GaussianKernel(double u) const {
  return ( ONE_OVER_ROOT_TWOPI * exp( -0.5 * u * u ) );
}

Analysis::RetType Analysis_KDE::Analyze() {
  Dimension& Xdim = output_->Dim(0);
  DataSet_1D const& In = static_cast<DataSet_1D const&>( *data_ );
  // Set output set dimensions from input set if necessary.
  if (!Xdim.MinIsSet())
    Xdim.SetMin( In.Min() );
  if (!Xdim.MaxIsSet())
    Xdim.SetMax( In.Max() );
  if (Xdim.CalcBinsOrStep()) return Analysis::ERR;
  Xdim.PrintDim();
  // TODO: When calculating KL divergence check q_data_ dimension.

  // Allocate output set
  DataSet_double& Out = static_cast<DataSet_double&>( *output_ );
  Out.Resize( Xdim.Bins() );

  // Estimate bandwidth from normal distribution approximation if necessary.
  if (bandwidth_ < 0.0) {
    double stdev;
    In.Avg( stdev );
    double N_to_1_over_5 = pow( (double)In.Size(), (-1.0/5.0) );
    bandwidth_ = 1.06 * stdev * N_to_1_over_5;
    mprintf("\tDetermined bandwidth from normal distribution approximation: %f\n", bandwidth_);
  }
 
  double total = 0.0;
  if (q_data_ == 0) {
    // Calculate KDE, loop over input data
    for (unsigned int i = 0; i < In.Size(); i++) {
      double val = In.Dval(i);
      double increment = 1.0;
      total += increment;
      // Apply kernel across histogram
      for (unsigned int j = 0; j < Out.Size(); j++)
        Out[j] += (increment * (this->*Kernel_)( (Xdim.Coord(j) - val) / bandwidth_ ));
    }
  } else {
    // Calculate Kullback-Leibler divergence vs time
    DataSet_1D const& Qdata = static_cast<DataSet_1D const&>( *q_data_ );
    size_t dataSize = In.Size();
    if (dataSize != Qdata.Size()) {
      mprintf("Warning: Size of %s (%zu) != size of %s (%zu)\n",
                In.Legend().c_str(), In.Size(), Qdata.Legend().c_str(), Qdata.Size());
      dataSize = std::min( dataSize, Qdata.Size() );
      mprintf("Warning:  Only using %zu data points.\n", dataSize);
    }
    DataSet_float& klOut = static_cast<DataSet_float&>( *kldiv_ );
    std::vector<double> Qhist( Xdim.Bins(), 0.0 ); // Raw Q histogram.
    klOut.Resize( dataSize ); // Hold KL div vs time
    // Loop over input P and Q data
    unsigned int nInvalid = 0;
    for (unsigned int i = 0; i < dataSize; i++) {
      //mprintf("Frame %i\n", i); // DEBUG
      double increment = 1.0;
      total += increment;
      // Apply kernel across P and Q, calculate KL divergence as we go. 
      double val_p = In.Dval(i);
      double val_q = Qdata.Dval(i);
      double KL = 0.0;
      bool validPoint = true;
      for (unsigned int j = 0; j < Out.Size(); j++) {
        double xcrd = Xdim.Coord(j);
        Out[j]   += (increment * (this->*Kernel_)( (xcrd - val_p) / bandwidth_ ));
        Qhist[j] += (increment * (this->*Kernel_)( (xcrd - val_q) / bandwidth_ ));
        //mprintf("\tBin %i: P=%f\tQ=%f\n", j, Out[j], Qhist[j]); // DEBUG
        if (validPoint) {
          // Normalize for this frame
          double Pnorm = Out[j]   / (total * bandwidth_);
          double Qnorm = Qhist[j] / (total * bandwidth_);
          // Q and P must either both be zero or both > 0.
          // If Q and P are both 0, interpret as 0 because lim(x->0){(x * ln(x)) = 0}
          // Otherwise, point is not valid and will be skipped.
          if (Pnorm != 0.0 && Qnorm != 0.0) {
            KL += ( log( Pnorm / Qnorm ) * Pnorm );
            validPoint = true;
          } else {
            if (Pnorm == 0.0 && Qnorm == 0.0)
              validPoint = true;
            else
              validPoint = false;
          }
        }
      }
      //mprintf("  KL= %f\n", KL); // DEBUG
      if (validPoint) {
        //mprintf("  POINT IS VALID.\n"); // DEBUG
        klOut[i] = (float)KL;
      } else {
        //mprintf("Warning:\tKullback-Leibler divergence is undefined for frame %u\n", i+1);
        //mprintf("  POINT IS NOT VALID.\n"); // DEBUG
        nInvalid++;
      }
    }
    if (nInvalid > 0)
      mprintf("Warning:\tKullback-Leibler divergence was undefined for %u frames.\n", nInvalid);
  }

  // Normalize
  for (unsigned int j = 0; j < Out.Size(); j++)
    Out[j] /= (total * bandwidth_);

  return Analysis::OK;
}