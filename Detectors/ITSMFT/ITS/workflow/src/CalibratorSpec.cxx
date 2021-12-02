/// @file   CalibratorSpec.cxx

#include <vector>

#include "TGeoGlobalMagField.h"

// ROOT includes
#include "TF1.h"
#include "TGraph.h"
#include "TH2D.h"

#include "FairLogger.h"

#include "Framework/ControlService.h"
#include "Framework/ConfigParamRegistry.h"
#include "ITSWorkflow/TrackerSpec.h"
#include "DataFormatsITSMFT/CompCluster.h"
#include "DataFormatsITS/TrackITS.h"
#include "SimulationDataFormat/MCCompLabel.h"
#include "SimulationDataFormat/MCTruthContainer.h"
#include "DataFormatsITSMFT/ROFRecord.h"

#include "Field/MagneticField.h"
#include "DetectorsBase/GeometryManager.h"
#include "DetectorsBase/Propagator.h"
#include "ITSBase/GeometryTGeo.h"
#include "DetectorsCommonDataFormats/NameConf.h"

#include "ITSWorkflow/CalibratorSpec.h"
#include <sstream>
#include <stdlib.h>

#include <vector>
#include <fmt/format.h>
#include <boost/lexical_cast.hpp>
#include <DPLUtils/RawParser.h>
#include <DPLUtils/DPLRawParser.h>


// Insert Digit stuff here
#include "DataFormatsITSMFT/Digit.h"
#include "DataFormatsITSMFT/ClusterPattern.h"
#include "DataFormatsITSMFT/ROFRecord.h"

#include "DetectorsCalibration/TimeSlotCalibration.h"
#include "DetectorsCalibration/TimeSlot.h"

using namespace o2::framework;
using namespace o2::itsmft;
using namespace o2::header;

namespace o2
{
namespace its
{

using namespace o2::framework;

// Define error function for ROOT fitting
int NINJ = 50;
double erf(double *xx, double *par) {
    return (NINJ / 2) * TMath::Erf((xx[0] - par[0]) / (sqrt(2) * par[1])) + (NINJ / 2);
}

//////////////////////////////////////////////////////////////////////////////
// Default constructor
template <class Mapping>
ITSCalibrator<Mapping>::ITSCalibrator() {
    mSelfName = o2::utils::Str::concat_string(Mapping::getName(), "ITSCalibrator");
    for (short int i = 0; i < this->nInj; i++) { this->x[i] = i + 1; }
}


//////////////////////////////////////////////////////////////////////////////
// Default deconstructor
template <class Mapping>
ITSCalibrator<Mapping>::~ITSCalibrator() {
    // Clear dynamic memory

    // Delete all the dynamically created TH2F for each chip
    for (auto const& th : this->thresholds) { delete th.second; }

    delete[] this->x;
}


//////////////////////////////////////////////////////////////////////////////
template <class Mapping>
void ITSCalibrator<Mapping>::init(InitContext& ic) {
    LOGF(info, "ITSCalibrator init...", mSelfName);

    o2::base::GeometryManager::loadGeometry();
    mGeom = o2::its::GeometryTGeo::Instance();

    // Initialize text file to save some processing output
    this->thrfile.open("thrfile.txt");

    //mChipsBuffer.resize(2);
    mChipsBuffer.resize(mGeom->getNumberOfChips());

    //outfile.open("thrfile.txt");
    //outfile << "ChipID Row nHits Charge(DAC)\n";
}


//////////////////////////////////////////////////////////////////////////////
// Initialize arrays of chip columns/rows for ROOT histograms
template <class Mapping>
void ITSCalibrator<Mapping>::get_row_col_arr(const short int & chipID, short int & nRow,
        float** row_arr_old, short int & nCol, float** col_arr_old) {

    // Set bin edges at the midpoints giving 1-indexing (0.5, 1.5, 2.5, ...)
    float* col_arr_new = new float[this->NCols];
    for (short int i = 0; i < this->NCols; i++) { col_arr_new[i] = i + 0.5; }
    *col_arr_old = col_arr_new;

    float* row_arr_new = new float[nRow];
    for (short int i = 0; i < this->NRows; i++) { row_arr_new[i] = i + 0.5; }
    *row_arr_old = row_arr_new;

    return;
}


//////////////////////////////////////////////////////////////////////////////
// Returns upper / lower limits for threshold determination.
// data is the number of trigger counts per charge injected;
// x is the array of charge injected values;
// NPoints is the length of both arrays.
template <class Mapping>
bool ITSCalibrator<Mapping>::FindUpperLower (const short int* data, const short int* x,
    const short int & NPoints, short int & Lower, short int & Upper) {

  // Initialize (or re-initialize) Upper and Lower
  Upper = -1;
  Lower = -1;

  for (int i = 0; i < NPoints; i ++) {
    if (data[i] >= this->nInj) {
      Upper = i;
      break;
    }
  }

  if (Upper == -1) return false;
  for (int i = NPoints - 1; i > 0; i--) {
    if (data[i] == 0) {
      Lower = i;
      break;
    }
  }

  // If search was successful, return central x value
  if ((Lower == -1) || (Upper < Lower)) return false;
  return true;
}

//////////////////////////////////////////////////////////////////////////////
// Returns estimate for the starting point of ROOT fitting
// data is the number of trigger counts per charge injected;
// x is the array of charge injected values;
// NPoints is the length of both arrays.
template <class Mapping>
float ITSCalibrator<Mapping>::FindStart (const short int* data,
    const short int* x, const short int & NPoints) {

  // Find the lower and upper edge of the threshold scan
  short int Lower, Upper;
  if (!FindUpperLower(data, x, NPoints, Lower, Upper)) return -1;
  return (x[Upper] + x[Lower]) / 2;
}


//////////////////////////////////////////////////////////////////////////////
// Use ROOT to find the threshold and noise via S-curve fit
// data is the number of trigger counts per charge injected;
// x is the array of charge injected values;
// NPoints is the length of both arrays.
// Final three pointers are updated with results from the fit
template <class Mapping>
bool ITSCalibrator<Mapping>::GetThreshold(const short int* data, const short int* x,
    const short int & NPoints, float *thresh, float *noise, float *chi2) {

  TGraph *g      = new TGraph(NPoints, (int*) x, (int*) data);
  TF1    *fitfcn = new TF1("fitfcn", erf, 0, 1500, 2);
  float  Start   = this->FindStart(data, x, NPoints);

  if (Start < 0) {
    //std::cerr << "ERROR: start-finding unsuccessful\n";
    throw Start;
  }

  // Initialize starting parameters
  fitfcn->SetParameter(0, Start);
  fitfcn->SetParameter(1, 8);

  fitfcn->SetParName(0, "Threshold");
  fitfcn->SetParName(1, "Noise");

  //g->SetMarkerStyle(20);
  //g->Draw("AP");
  g->Fit("fitfcn", "Q");

  *noise = fitfcn->GetParameter(1);
  *thresh = fitfcn->GetParameter(0);
  *chi2 = fitfcn->GetChisquare() / fitfcn->GetNDF();

  g->Delete();
  fitfcn->Delete();
  return true;
}


//////////////////////////////////////////////////////////////////////////////
// Use ROOT to find the threshold and noise via derivative method
// data is the number of trigger counts per charge injected;
// x is the array of charge injected values;
// NPoints is the length of both arrays.
// Final three pointers are updated with results from the fit
template <class Mapping>
bool ITSCalibrator<Mapping>::GetThresholdAlt(const short int* data, const short int* x,
    const short int & NPoints, float *thresh, float *noise, float *chi2) {

  // Find lower & upper values of the S-curve region
  int Lower, Upper;
  if (!this->FindUpperLower(data, x, NPoints, Lower, Upper) || Lower == Upper) {
    //std::cerr << "ERROR: start-finding unsuccessful\n";
    int vals[] = { Lower, Upper };
    throw vals;
  }

  int deriv_size = Upper - Lower;
  float* deriv = new float[deriv_size]; // Maybe better way without ROOT?
  TH1F* h_deriv = new TH1F("h_deriv", "h_deriv", deriv_size, x[Lower], x[Upper]);

  // Fill array with derivatives
  for (int i = 0; i + Lower < Upper; i++) {
    deriv[i] = (data[i+Lower+1] - data[i+Lower]) / (x[i+Lower+1] - x[i+Lower]);
    h_deriv->SetBinContent(i+1, deriv[i]);  // i+1 because of 1-indexing in ROOT
  }

  // Find the mean of the derivative distribution
  TF1* fit = new TF1("fit", "gaus", x[Lower], x[Upper]);
  h_deriv->Fit(fit);

  // Parameter 0 = normalization factor
  // Parameter 1 = mean (mu)
  // Parameter 2 = standard deviation (sigma)
  *noise = fit->GetParameter(2);
  *thresh = fit->GetParameter(1);
  *chi2 = fit->GetChisquare() / fit->GetNDF();

  delete[] deriv;
  delete h_deriv;
  return true;
}


//////////////////////////////////////////////////////////////////////////////
// Reset the current row, and create a new vector to store hits for that row
template <class Mapping>
void ITSCalibrator<Mapping>::reset_row_hitmap(const short int& chipID, const short int& row) {
   // Update current row of chip
   this->currentRow[chipID] = row;

   // Reset pixel hit counts for the chip, new empty hitvec
   // Create a 2D vector to store hits
   // Outer dim = column, inner dim = charge
   this->pixelHits[chipID] = std::vector<std::vector<short int>> (this->NCols, std::vector<short int> (this->nCharge, 0));
}


//////////////////////////////////////////////////////////////////////////////
// Initialize any desired output objects for writing threshold info per-chip
template <class Mapping>
void ITSCalibrator<Mapping>::init_chip_data(const short int& chipID) {
    // Create a TH2 to save the threshold info
    const char* th_ChipID = ("thresh_chipID" + std::to_string(chipID)).c_str();

    short int nRow, nCol;
    float* row_arr; float* col_arr;
    get_row_col_arr(chipID, nRow, &row_arr, nCol, &col_arr);
    //LOG(info)<<row_arr[400];
    TH2F* th = new TH2F(th_ChipID, th_ChipID, nCol-1, col_arr, nRow-1, row_arr);  // x = rows, y = columns
    this->thresholds[chipID] = th;
    delete[] row_arr, col_arr;
}


//////////////////////////////////////////////////////////////////////////////
// Run threshold extraction on completed row and update memory
template <class Mapping>
void ITSCalibrator<Mapping>::extract_thresh_row(const short int& chipID, const short int& row) {

    int lay, sta, ssta, mod, chip, chipInMod; // layer, stave, sub stave, module, chip

    for (short int col_i = 0; col_i < this->NCols; col_i++) {

        float threshold, noise, chi2;
        // Convert counts to C array for the fitting function
        const short int* data = &(this->pixelHits[chipID][col_i][0]);

        // Do the threshold fit
        try { GetThreshold(data, this->x, this->nCharge, &threshold, &noise, &chi2); }

        // Print helpful info to output file for debugging
        // col+1 because of ROOT 1-indexing (I think row already has the +1)
        catch (int i) {
            this->thrfile << "ERROR: start-finding unsuccessful for chipID " << chipID
                          << " row " << row << " column " << (col_i + 1) << '\n';
            continue;
        }
        catch (int* i) {
            this->thrfile << "ERROR: start-finding unsuccessful for chipID " << chipID
                          << " row " << row << " column " << (col_i + 1) << '\n';
            continue;
        }

        //this->thrfile << chipID << " " << row << " " << (col_i + 1) << " ::  th: "
        //              << threshold << ", noise: " << noise << ", chi2: " << chi2 << '\n';

        // Update ROOT histograms
        this->thresholds[chipID]->SetBinContent(col_i + 1, row, threshold);
        this->thresholds[chipID]->SetBinError(col_i + 1, row, noise);
        // TODO: store the chi2 info somewhere useful

        // Obtain specific chip information from the chip ID (layer, stave, ...)
        mp.expandChipInfoHW(chipID, lay, sta, ssta, mod, chipInMod);
        // TODO use this info when writing to CCDB
        LOG(info) << "Stave: " << sta << " | Half-stave: " << ssta << " | Module: " << mod
                  << " | Chip ID: " << chipID << " | Row: " << row << " | Col: " << (col_i + 1)
                  << " | Threshold: " << threshold << " | Noise: " << noise << " | Chi^2: " << chi2;
    }
}


//////////////////////////////////////////////////////////////////////////////
// Write to any desired output objects after saving threshold info in memory
template <class Mapping>
void ITSCalibrator<Mapping>::update_output(const short int& chipID) {
    // Initialize ROOT output file
    TFile* tf = new TFile("threshold_scan.root", "UPDATE");

    // Update the ROOT file with most recent histo
    tf->cd();
    this->thresholds[chipID]->Write(0, TObject::kOverwrite);

    // Close file and clean up memory
    tf->Close();
    delete tf;
}


//////////////////////////////////////////////////////////////////////////////
// Main running function
// Get info from previous stf decoder workflow, then loop over readout frames
//     (ROFs) to count hits and extract thresholds
template <class Mapping>
void ITSCalibrator<Mapping>::run(ProcessingContext& pc)
{
    this->thrfile.open("thrfile.txt", std::ios_base::app);

    int TriggerId = 0;

    //mDecoder->startNewTF(pc.inputs());
    auto orig = Mapping::getOrigin();

    // Calibration vector
    const auto calibs = pc.inputs().get<gsl::span<o2::itsmft::GBTCalibData>>("calib");
    const auto digits = pc.inputs().get<gsl::span<o2::itsmft::Digit>>("digits");
    const auto ROFs = pc.inputs().get<gsl::span<o2::itsmft::ROFRecord>>("digitsROF");
    const auto tfcounter = o2::header::get<o2::framework::DataProcessingHeader*>(pc.inputs().get("digitsROF").header)->startTime;

    // Store some lengths for convenient looping
    const short int nROF = (short int) ROFs.size();
    //const short int nCals = (short int) calibs.size();

    LOG(info) << "Processing TF# " << tfcounter;

    // Loop over readout frames (usually only 1, sometimes 2)
    short int iROF = 0;  // keep track of which ROF we are looking at
    for (const auto& rof : ROFs) {
        auto digitsInFrame = rof.getROFData(digits);

        // Find the correct charge and row values for this ROF
        short int charge = -1;
        short int row = -1;
        for (short int iRU = 0; iRU < this->nRUs; iRU++) {
            const auto& calib = calibs[iROF * nRUs + iRU];
            if (calib.calibUserField != 0) {
                if (charge >= 0) { LOG(info) << "WARNING: more than one charge detected!"; }
                // Divide calibration word (24-bit) by 2^16 to get the first 8 bits
                charge = (short int) (170 - calib.calibUserField / 65536);
                // Last 16 bits should be the row (only uses up to 9 bits)
                row = (short int) (calib.calibUserField & 0xffff);
                //LOG(info) << "calib size: " << calib.size() << " | Index: " << i
                //          << " | calib Vec Counter: " << calib.calibCounter << " | charge: " << charge;
                //break;
            }
        }
        // Update iROF for the next charge if necessary
        iROF++;

        // If a charge was not found, throw an error and skip this ROF
        if (charge < 0) {
            LOG(info) << "WARNING: Charge not updated" << '\n';
            //thrfile   << "WARNING: Charge not updated" << '\n';
        } else if (charge == 0) {
            LOG(info) << "WARNING: charge == 0" << '\n';
            //thrfile   << "WARNING: charge == 0" << '\n';
        } else {

            for (const auto& d : digitsInFrame) {
                short int chipID = (short int) d.getChipIndex();
                short int col = (short int) d.getColumn();

                // Row should be the same for the whole ROF so do not have to check here
                // TODO test that this is working ok
                //short int row = (short int) d.getRow();

                // Check if chip hasn't appeared before
                if (this->currentRow.find(chipID) == this->currentRow.end()) {
                    // Update the current row and hit map
                    this->reset_row_hitmap(chipID, row);
                    // Create data structures to hold the threshold info
                    this->init_chip_data(chipID);

                // Check if we have a new row on an already existing chip
                } else if (this->currentRow[chipID] != row) {
                    LOG(info) << "Extracting threshold values from previous row";
                    this->extract_thresh_row(chipID, this->currentRow[chipID]);
                    // Write thresholds to output data structures
                    this->update_output(chipID);
                    // Reset row & hitmap for the new row
                    this->reset_row_hitmap(chipID, row);
                }

                // Increment the number of counts for this pixel
                this->pixelHits[chipID][col][charge-1]++;

            } // for (digits)

        } // for (rofs)
    }

    TriggerId = 0;
    mTFCounter++;
    delete[] x;
    thrfile.close();
}


//////////////////////////////////////////////////////////////////////////////
template <class Mapping>
void ITSCalibrator<Mapping>::endOfStream(EndOfStreamContext& ec)
{
    LOGF(info, "endOfStream report:", mSelfName);
//    if (mDecoder) {
//        mDecoder->printReport(true, true);
//    }
}


//////////////////////////////////////////////////////////////////////////////
DataProcessorSpec getITSCalibratorSpec()
{
    o2::header::DataOrigin detOrig = o2::header::gDataOriginITS;
    std::vector<InputSpec> inputs;
    //inputs.emplace_back("RAWDATA", ConcreteDataTypeMatcher{"ITS", "RAWDATA"}, Lifetime::Timeframe);
    inputs.emplace_back("digits", detOrig, "DIGITS", 0, Lifetime::Timeframe);
    inputs.emplace_back("digitsROF", detOrig, "DIGITSROF", 0, Lifetime::Timeframe);
    inputs.emplace_back("calib", detOrig, "GBTCALIB", 0, Lifetime::Timeframe);

    std::vector<OutputSpec> outputs;
    outputs.emplace_back("ITS", "CLUSTERSROF", 0, Lifetime::Timeframe);

    auto orig = o2::header::gDataOriginITS;

    return DataProcessorSpec{
        "its-calibrator",
        inputs,
        outputs,
        AlgorithmSpec{adaptFromTask<ITSCalibrator<ChipMappingITS>>()},
        // here I assume ''calls'' init, run, endOfStream sequentially...
        Options{}
    };
}
} // namespace its
} // namespace o2
