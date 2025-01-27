/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

 Module:       FGWinds.cpp
 Author:       Jon Berndt, Tony Peden, Andreas Gaeb
 Date started: Extracted from FGAtmosphere, which originated in 1998
               5/2011
 Purpose:      Models winds, gusts, turbulence, and other atmospheric
               disturbances
 Called by:    FGFDMExec

 ------------- Copyright (C) 2011  Jon S. Berndt (jon@jsbsim.org) -------------

 This program is free software; you can redistribute it and/or modify it under
 the terms of the GNU Lesser General Public License as published by the Free
 Software Foundation; either version 2 of the License, or (at your option) any
 later version.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 details.

 You should have received a copy of the GNU Lesser General Public License along
 with this program; if not, write to the Free Software Foundation, Inc., 59
 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

 Further information about the GNU Lesser General Public License can also be
 found on the world wide web at http://www.gnu.org.

FUNCTIONAL DESCRIPTION
--------------------------------------------------------------------------------

HISTORY
--------------------------------------------------------------------------------

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
COMMENTS, REFERENCES,  and NOTES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
[1]   Anderson, John D. "Introduction to Flight, Third Edition", McGraw-Hill,
      1989, ISBN 0-07-001641-0

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
INCLUDES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#include "FGWinds.h"
#include "FGFDMExec.h"
#include "math/FGTable.h"

using namespace std;

namespace JSBSim {

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
CLASS IMPLEMENTATION
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// square a value, but preserve the original sign

/*
static inline double square_signed (double value)
{
    if (value < 0)
        return value * value * -1;
    else
        return value * value;
}
*/

/// simply square a value
constexpr double sqr(double x) { return x*x; }

FGWinds::FGWinds(FGFDMExec* fdmex) : FGModel(fdmex)
{
  Name = "FGWinds";

  MagnitudedAccelDt = MagnitudeAccel = Magnitude = TurbDirection = 0.0;
  SetTurbType( ttMilspec );
  TurbGain = 1.0;
  TurbRate = 10.0;
  Rhythmicity = 0.1;
  spike = target_time = strength = 0.0;
  wind_from_clockwise = 0.0;
  psiw = 0.0;

  convVeloScale = 0.0; // meters
  convVeloScaleSTD = 0;
  convLayerThickness = 1000; //meters
  convLayerThicknessSTD = 0;
  thermalAreaWidth = 1000; //meters
  thermalAreaHeight = 1000; //meters

  vGustNED.InitMatrix();
  vTurbulenceNED.InitMatrix();
  vCosineGust.InitMatrix();
  vThermals.InitMatrix();

  // Milspec turbulence model
  windspeed_at_20ft = 0.;
  probability_of_exceedence_index = 0;
  POE_Table = new FGTable(7,12);
  // this is Figure 7 from p. 49 of MIL-F-8785C
  // rows: probability of exceedance curve index, cols: altitude in ft
  *POE_Table
           << 500.0 << 1750.0 << 3750.0 << 7500.0 << 15000.0 << 25000.0 << 35000.0 << 45000.0 << 55000.0 << 65000.0 << 75000.0 << 80000.0
    << 1   <<   3.2 <<    2.2 <<    1.5 <<    0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0
    << 2   <<   4.2 <<    3.6 <<    3.3 <<    1.6 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0
    << 3   <<   6.6 <<    6.9 <<    7.4 <<    6.7 <<     4.6 <<     2.7 <<     0.4 <<     0.0 <<     0.0 <<     0.0 <<     0.0 <<     0.0
    << 4   <<   8.6 <<    9.6 <<   10.6 <<   10.1 <<     8.0 <<     6.6 <<     5.0 <<     4.2 <<     2.7 <<     0.0 <<     0.0 <<     0.0
    << 5   <<  11.8 <<   13.0 <<   16.0 <<   15.1 <<    11.6 <<     9.7 <<     8.1 <<     8.2 <<     7.9 <<     4.9 <<     3.2 <<     2.1
    << 6   <<  15.6 <<   17.6 <<   23.0 <<   23.6 <<    22.1 <<    20.0 <<    16.0 <<    15.1 <<    12.1 <<     7.9 <<     6.2 <<     5.1
    << 7   <<  18.7 <<   21.5 <<   28.4 <<   30.2 <<    30.7 <<    31.0 <<    25.2 <<    23.1 <<    17.5 <<    10.7 <<     8.4 <<     7.2;

  bind();
  Debug(0);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

FGWinds::~FGWinds()
{
  delete(POE_Table);
  Debug(1);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

bool FGWinds::InitModel(void)
{
  if (!FGModel::InitModel()) return false;

  psiw = 0.0;

  vGustNED.InitMatrix();
  vTurbulenceNED.InitMatrix();
  vCosineGust.InitMatrix();
  vThermals.InitMatrix();
  init_geod_lat = 0.0;
  init_long = 0.0;


  oneMinusCosineGust.gustProfile.Running = false;
  oneMinusCosineGust.gustProfile.elapsedTime = 0.0;

  return true;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

bool FGWinds::Run(bool Holding)
{
  if (FGModel::Run(Holding)) return true;
  if (Holding) return false;

  if (turbType != ttNone) Turbulence(in.AltitudeASL);
  if (oneMinusCosineGust.gustProfile.Running) CosineGust();
  if (convVeloScale != 0) {
    if (!initializedThermals) {InitThermals();}
    UpdateThermals();
  }

  vTotalWindNED = vWindNED + vGustNED + vCosineGust + vTurbulenceNED + vThermals;

   // psiw (Wind heading) is the direction the wind is blowing towards
  if (vWindNED(eX) != 0.0) psiw = atan2( vWindNED(eY), vWindNED(eX) );
  if (psiw < 0) psiw += 2*M_PI;

  Debug(2);
  return false;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//
// psi is the angle that the wind is blowing *towards*

void FGWinds::SetWindspeed(double speed)
{
  if (vWindNED.Magnitude() == 0.0) {
    psiw = 0.0;
    vWindNED(eNorth) = speed;
  } else {
    vWindNED(eNorth) = speed * cos(psiw);
    vWindNED(eEast) = speed * sin(psiw);
    vWindNED(eDown) = 0.0;
  }
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

double FGWinds::GetWindspeed(void) const
{
  return vWindNED.Magnitude();
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//
// psi is the angle that the wind is blowing *towards*

void FGWinds::SetWindPsi(double dir)
{
  double mag = GetWindspeed();
  psiw = dir;
  SetWindspeed(mag);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGWinds::Turbulence(double h)
{
  switch (turbType) {

  case ttCulp: {

    vTurbPQR(eP) = wind_from_clockwise;
    if (TurbGain == 0.0) return;

    // keep the inputs within allowable limts for this model
    if (TurbGain < 0.0) TurbGain = 0.0;
    if (TurbGain > 1.0) TurbGain = 1.0;
    if (TurbRate < 0.0) TurbRate = 0.0;
    if (TurbRate > 30.0) TurbRate = 30.0;
    if (Rhythmicity < 0.0) Rhythmicity = 0.0;
    if (Rhythmicity > 1.0) Rhythmicity = 1.0;

    // generate a sine wave corresponding to turbulence rate in hertz
    double time = FDMExec->GetSimTime();
    double sinewave = sin( time * TurbRate * 6.283185307 );

    double random = 0.0;
    if (target_time == 0.0) {
      strength = random = 1 - 2.0*(double(rand())/double(RAND_MAX));
      target_time = time + 0.71 + (random * 0.5);
    }
    if (time > target_time) {
      spike = 1.0;
      target_time = 0.0;
    }

    // max vertical wind speed in fps, corresponds to TurbGain = 1.0
    double max_vs = 40;

    vTurbulenceNED.InitMatrix();
    double delta = strength * max_vs * TurbGain * (1-Rhythmicity) * spike;

    // Vertical component of turbulence.
    vTurbulenceNED(eDown) = sinewave * max_vs * TurbGain * Rhythmicity;
    vTurbulenceNED(eDown)+= delta;
    if (in.DistanceAGL/in.wingspan < 3.0)
        vTurbulenceNED(eDown) *= in.DistanceAGL/in.wingspan * 0.3333;

    // Yaw component of turbulence.
    vTurbulenceNED(eNorth) = sin( delta * 3.0 );
    vTurbulenceNED(eEast) = cos( delta * 3.0 );

    // Roll component of turbulence. Clockwise vortex causes left roll.
    vTurbPQR(eP) += delta * 0.04;

    spike = spike * 0.9;
    break;
  }
  case ttMilspec:
  case ttTustin: {

    // an index of zero means turbulence is disabled
    // airspeed occurs as divisor in the code below
    if (probability_of_exceedence_index == 0 || in.V == 0) {
      vTurbulenceNED(eNorth) = vTurbulenceNED(eEast) = vTurbulenceNED(eDown) = 0.0;
      vTurbPQR(eP) = vTurbPQR(eQ) = vTurbPQR(eR) = 0.0;
      return;
    }

    // Turbulence model according to MIL-F-8785C (Flying Qualities of Piloted Aircraft)
    double b_w = in.wingspan, L_u, L_w, sig_u, sig_w;

      if (b_w == 0.) b_w = 30.;

    // clip height functions at 10 ft
    if (h <= 10.) h = 10;

    // Scale lengths L and amplitudes sigma as function of height
    if (h <= 1000) {
      L_u = h/pow(0.177 + 0.000823*h, 1.2); // MIL-F-8785c, Fig. 10, p. 55
      L_w = h;
      sig_w = 0.1*windspeed_at_20ft;
      sig_u = sig_w/pow(0.177 + 0.000823*h, 0.4); // MIL-F-8785c, Fig. 11, p. 56
    } else if (h <= 2000) {
      // linear interpolation between low altitude and high altitude models
      L_u = L_w = 1000 + (h-1000.)/1000.*750.;
      sig_u = sig_w = 0.1*windspeed_at_20ft
                    + (h-1000.)/1000.*(POE_Table->GetValue(probability_of_exceedence_index, h) - 0.1*windspeed_at_20ft);
    } else {
      L_u = L_w = 1750.; //  MIL-F-8785c, Sec. 3.7.2.1, p. 48
      sig_u = sig_w = POE_Table->GetValue(probability_of_exceedence_index, h);
    }

    // keep values from last timesteps
    // TODO maybe use deque?
    static double
      xi_u_km1 = 0, nu_u_km1 = 0,
      xi_v_km1 = 0, xi_v_km2 = 0, nu_v_km1 = 0, nu_v_km2 = 0,
      xi_w_km1 = 0, xi_w_km2 = 0, nu_w_km1 = 0, nu_w_km2 = 0,
      xi_p_km1 = 0, nu_p_km1 = 0,
      xi_q_km1 = 0, xi_r_km1 = 0;


    double
      T_V = in.totalDeltaT, // for compatibility of nomenclature
      sig_p = 1.9/sqrt(L_w*b_w)*sig_w, // Yeager1998, eq. (8)
      //sig_q = sqrt(M_PI/2/L_w/b_w), // eq. (14)
      //sig_r = sqrt(2*M_PI/3/L_w/b_w), // eq. (17)
      L_p = sqrt(L_w*b_w)/2.6, // eq. (10)
      tau_u = L_u/in.V, // eq. (6)
      tau_w = L_w/in.V, // eq. (3)
      tau_p = L_p/in.V, // eq. (9)
      tau_q = 4*b_w/M_PI/in.V, // eq. (13)
      tau_r =3*b_w/M_PI/in.V, // eq. (17)
      nu_u = GaussianRandomNumber(),
      nu_v = GaussianRandomNumber(),
      nu_w = GaussianRandomNumber(),
      nu_p = GaussianRandomNumber(),
      xi_u=0, xi_v=0, xi_w=0, xi_p=0, xi_q=0, xi_r=0;

    // values of turbulence NED velocities

    if (turbType == ttTustin) {
      // the following is the Tustin formulation of Yeager's report
      double
        omega_w = in.V/L_w, // hidden in nomenclature p. 3
        omega_v = in.V/L_u, // this is defined nowhere
        C_BL  = 1/tau_u/tan(T_V/2/tau_u), // eq. (19)
        C_BLp = 1/tau_p/tan(T_V/2/tau_p), // eq. (22)
        C_BLq = 1/tau_q/tan(T_V/2/tau_q), // eq. (24)
        C_BLr = 1/tau_r/tan(T_V/2/tau_r); // eq. (26)

      // all values calculated so far are strictly positive, except for
      // the random numbers nu_*. This means that in the code below, all
      // divisors are strictly positive, too, and no floating point
      // exception should occur.
      xi_u = -(1 - C_BL*tau_u)/(1 + C_BL*tau_u)*xi_u_km1
           + sig_u*sqrt(2*tau_u/T_V)/(1 + C_BL*tau_u)*(nu_u + nu_u_km1); // eq. (18)
      xi_v = -2*(sqr(omega_v) - sqr(C_BL))/sqr(omega_v + C_BL)*xi_v_km1
           - sqr(omega_v - C_BL)/sqr(omega_v + C_BL) * xi_v_km2
           + sig_u*sqrt(3*omega_v/T_V)/sqr(omega_v + C_BL)*(
                 (C_BL + omega_v/sqrt(3.))*nu_v
               + 2/sqrt(3.)*omega_v*nu_v_km1
               + (omega_v/sqrt(3.) - C_BL)*nu_v_km2); // eq. (20) for v
      xi_w = -2*(sqr(omega_w) - sqr(C_BL))/sqr(omega_w + C_BL)*xi_w_km1
           - sqr(omega_w - C_BL)/sqr(omega_w + C_BL) * xi_w_km2
           + sig_w*sqrt(3*omega_w/T_V)/sqr(omega_w + C_BL)*(
                 (C_BL + omega_w/sqrt(3.))*nu_w
               + 2/sqrt(3.)*omega_w*nu_w_km1
               + (omega_w/sqrt(3.) - C_BL)*nu_w_km2); // eq. (20) for w
      xi_p = -(1 - C_BLp*tau_p)/(1 + C_BLp*tau_p)*xi_p_km1
           + sig_p*sqrt(2*tau_p/T_V)/(1 + C_BLp*tau_p) * (nu_p + nu_p_km1); // eq. (21)
      xi_q = -(1 - 4*b_w*C_BLq/M_PI/in.V)/(1 + 4*b_w*C_BLq/M_PI/in.V) * xi_q_km1
           + C_BLq/in.V/(1 + 4*b_w*C_BLq/M_PI/in.V) * (xi_w - xi_w_km1); // eq. (23)
      xi_r = - (1 - 3*b_w*C_BLr/M_PI/in.V)/(1 + 3*b_w*C_BLr/M_PI/in.V) * xi_r_km1
           + C_BLr/in.V/(1 + 3*b_w*C_BLr/M_PI/in.V) * (xi_v - xi_v_km1); // eq. (25)

    } else if (turbType == ttMilspec) {
      // the following is the MIL-STD-1797A formulation
      // as cited in Yeager's report
      xi_u = (1 - T_V/tau_u)  *xi_u_km1 + sig_u*sqrt(2*T_V/tau_u)*nu_u;  // eq. (30)
      xi_v = (1 - 2*T_V/tau_u)*xi_v_km1 + sig_u*sqrt(4*T_V/tau_u)*nu_v;  // eq. (31)
      xi_w = (1 - 2*T_V/tau_w)*xi_w_km1 + sig_w*sqrt(4*T_V/tau_w)*nu_w;  // eq. (32)
      xi_p = (1 - T_V/tau_p)  *xi_p_km1 + sig_p*sqrt(2*T_V/tau_p)*nu_p;  // eq. (33)
      xi_q = (1 - T_V/tau_q)  *xi_q_km1 + M_PI/4/b_w*(xi_w - xi_w_km1);  // eq. (34)
      xi_r = (1 - T_V/tau_r)  *xi_r_km1 + M_PI/3/b_w*(xi_v - xi_v_km1);  // eq. (35)
    }

    // rotate by wind azimuth and assign the velocities
    double cospsi = cos(psiw), sinpsi = sin(psiw);
    vTurbulenceNED(eNorth) =  cospsi*xi_u + sinpsi*xi_v;
    vTurbulenceNED(eEast) = -sinpsi*xi_u + cospsi*xi_v;
    vTurbulenceNED(eDown) = xi_w;

    vTurbPQR(eP) =  cospsi*xi_p + sinpsi*xi_q;
    vTurbPQR(eQ) = -sinpsi*xi_p + cospsi*xi_q;
    vTurbPQR(eR) = xi_r;

    // vTurbPQR is in the body fixed frame, not NED
    vTurbPQR = in.Tl2b*vTurbPQR;

    // hand on the values for the next timestep
    xi_u_km1 = xi_u; nu_u_km1 = nu_u;
    xi_v_km2 = xi_v_km1; xi_v_km1 = xi_v; nu_v_km2 = nu_v_km1; nu_v_km1 = nu_v;
    xi_w_km2 = xi_w_km1; xi_w_km1 = xi_w; nu_w_km2 = nu_w_km1; nu_w_km1 = nu_w;
    xi_p_km1 = xi_p; nu_p_km1 = nu_p;
    xi_q_km1 = xi_q;
    xi_r_km1 = xi_r;

  }
  default:
    break;
  }

  TurbDirection = atan2( vTurbulenceNED(eEast), vTurbulenceNED(eNorth))*radtodeg;

}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

double FGWinds::CosineGustProfile(double startDuration, double steadyDuration, double endDuration, double elapsedTime)
{
  double factor = 0.0;
  if (elapsedTime >= 0 && elapsedTime <= startDuration) {
    factor = (1.0 - cos(M_PI*elapsedTime/startDuration))/2.0;
  } else if (elapsedTime > startDuration && (elapsedTime <= (startDuration + steadyDuration))) {
    factor = 1.0;
  } else if (elapsedTime > (startDuration + steadyDuration) && elapsedTime <= (startDuration + steadyDuration + endDuration)) {
    factor = (1-cos(M_PI*(1-(elapsedTime-(startDuration + steadyDuration))/endDuration)))/2.0;
  } else {
    factor = 0.0;
  }

  return factor;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGWinds::CosineGust()
{
  struct OneMinusCosineProfile& profile = oneMinusCosineGust.gustProfile;

  double factor = CosineGustProfile( profile.startupDuration,
                                     profile.steadyDuration,
                                     profile.endDuration,
                                     profile.elapsedTime);
  // Normalize the gust wind vector
  oneMinusCosineGust.vWind.Normalize();

  if (oneMinusCosineGust.vWindTransformed.Magnitude() == 0.0) {
    switch (oneMinusCosineGust.gustFrame) {
    case gfBody:
      oneMinusCosineGust.vWindTransformed = in.Tl2b.Inverse() * oneMinusCosineGust.vWind;
      break;
    case gfWind:
      oneMinusCosineGust.vWindTransformed = in.Tl2b.Inverse() * in.Tw2b * oneMinusCosineGust.vWind;
      break;
    case gfLocal:
      // this is the native frame - and the default.
      oneMinusCosineGust.vWindTransformed = oneMinusCosineGust.vWind;
      break;
    default:
      break;
    }
  }

  vCosineGust = factor * oneMinusCosineGust.vWindTransformed * oneMinusCosineGust.magnitude;

  profile.elapsedTime += in.totalDeltaT;

  if (profile.elapsedTime > (profile.startupDuration + profile.steadyDuration + profile.endDuration)) {
    profile.Running = false;
    profile.elapsedTime = 0.0;
    oneMinusCosineGust.vWindTransformed.InitMatrix();
    vCosineGust.InitMatrix(0);
  }
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// Need to initialize the number of thermals and their positions based on the input parameters
void FGWinds::InitThermals()
{

  // Find radius of thermals at 50m
  double r2 = (0.102 * pow(50.0/convLayerThickness,1.0/3.0))*(1-(0.25*50.0/convLayerThickness))*convLayerThickness;

  numThermals = ceil(0.6*thermalAreaHeight*thermalAreaWidth/(convLayerThickness*r2));

  // Initialize the vectors to the correct sizes
  thermalLocations.assign(numThermals, FGColumnVector3());
  thermalStrengths.assign(numThermals, 0);
  thermalHeights.assign(numThermals, 0);

  // For each thermal, set their locations, strengths, and heights
  for (int i = 0; i < numThermals; i++) {
    // Generate the X and Y offsets for the thermals as random numbers
    thermalLocations[i](1) = (((float) rand() / RAND_MAX) - 0.5) * thermalAreaWidth * 3.281;
    thermalLocations[i](2) = (((float) rand() / RAND_MAX) - 0.5) * thermalAreaHeight * 3.281;
    thermalLocations[i](3) = 0;

    thermalStrengths[i] = convVeloScale;
    thermalHeights[i] = convLayerThickness;
  }
  initializedThermals = true;
}

// Update thermals takes the aircraft's position and calculates the vertical velocity component from
// the locations of all the thermals around. It also propogates any time related changes to the thermals
void FGWinds::UpdateThermals()
{

  vThermals.InitMatrix(0); ///Initialize the thermal updraft veloicty to 0

  // If we haven't set the initial's position, set it to where the aircraft is
  if (!have_initial_location & in.DistanceAGL > 0) {
    initLocation = FGLocation(in.vLocation);
    init_geod_lat = in.vLocation.GetGeodLatitudeRad();
    init_long = in.vLocation.GetLongitude();
    init_geod_altitude = in.vLocation.GetGeodAltitude();
    have_initial_location = true;
  }

  // Need to find the distance to the nearest thermal and which thermal that is in the list
  vector<double> dist_to_thermals;
  dist_to_thermals.assign(numThermals, 0);
  double dist_to_nearest_thermal = sqrt(pow(thermalAreaWidth, 2) + pow(thermalAreaHeight, 2)); // Initialize to the biggest it could be
  int best_thermal_index = -1;
  FGLocation tempLocation;
  FGLocation cur_thermal_location_global = FGLocation(initLocation);
  FGColumnVector3 cur_thermal_location_local;

  // For each thermal in the list
  for (int i = 0; i < numThermals; i++) {

    // Find the global location of the current thermal being tested based on the initial position
    // Need to do some trickery because the ellipse on the location needs to be set a weird way
    tempLocation = initLocation.LocalToLocation(thermalLocations[i]);
    cur_thermal_location_global.SetLatitude(tempLocation.GetLatitude());
    cur_thermal_location_global.SetLongitude(tempLocation.GetLongitude());

    // Find the distance between the vehicle and the thermal but ignore altitude changes
    cur_thermal_location_global.SetRadius(in.vLocation.GetRadius());

    dist_to_thermals[i] = in.vLocation.GetDistanceTo(cur_thermal_location_global.GetLongitude(),
                                                         cur_thermal_location_global.GetGeodLatitudeRad());

    // Update the distance to the nearest thermal if this one is closer
    if (dist_to_nearest_thermal > dist_to_thermals[i]) {
      dist_to_nearest_thermal = dist_to_thermals[i];
      best_thermal_index = i; // Also remember the index for getting the strength and height
    }
  }

  double test_radius = 200;

  // inputs to this function should be in meters
  float AGL = 0.1;
  if (in.vLocation.GetGeodAltitude() - init_geod_altitude > 0) {
    AGL = (in.vLocation.GetGeodAltitude() - init_geod_altitude)/3.281;
  } 
  vThermals(3) = GetThermalEffect(dist_to_nearest_thermal / 3.281, thermalStrengths[best_thermal_index], thermalHeights[best_thermal_index],
                                  AGL, numThermals);

  // std::cout << vThermals(3) << endl;

}

float FGWinds::GetThermalEffect(float distance, float thermal_strength, float thermal_height, float test_altitude, int num_thermals) {
  vector<float> r1r2shape = {{0.1400, 0.2500, 0.3600, 0.4700, 0.5800, 0.6900, 0.8000}};
  vector<vector<float>> Kshape = {{1.5352, 2.5826, -0.0113, -0.1950, 0.0008},
                                  {1.5265, 3.6054, -0.0176, -0.1265, 0.0005}, 
                                  {1.4866, 4.8356, -0.0320, -0.0818, 0.0001},
                                  {1.2042, 7.7904,  0.0848, -0.0445, 0.0001},
                                  {0.8816, 13.9720, 0.3404, -0.0216, 0.0001},
                                  {0.7067, 23.9940, 0.5689, -0.0099, 0.0002},
                                  {0.6189, 42.7965, 0.7157, -0.0033, 0.0001}};
  bool sflag = true;
  float current_strength = thermal_strength;

  // Find radius using altitude and thermal height
  float alt_ratio = test_altitude / thermal_height;
  float outer_rad = (0.102 * pow(alt_ratio, 1.0/3.0)) * (1-(0.25 * alt_ratio)) * thermal_height;

  // Find the average updraft strength
  float normalized_strength = (pow(alt_ratio, 1.0/3.0)) * (1 - 1.1*alt_ratio) * current_strength;

  // Calculate the inner radius of the updraft
  
  float inner_outer_ratio;
  if (outer_rad < 10){
    outer_rad = 10;
  }
  if (outer_rad < 600) {
    inner_outer_ratio = 0.0011*outer_rad+0.14;
  } else {
    inner_outer_ratio = 0.8;
  }
  float inner_rad = inner_outer_ratio * outer_rad;

  // Calculate the strength at the center of the updraft
  float core_strength = (3 * normalized_strength * (pow(outer_rad,3)-pow(outer_rad,2) * inner_rad)) / (pow(outer_rad, 3) - pow(inner_rad, 3));


  // Calculate the updraft velocity
  float radius_ratio = distance / outer_rad;
  float smooth_strength;
  float ka;
  float kb;
  float kc;
  float kd;
  if (alt_ratio < 1) {
    if (radius_ratio < (0.5 * r1r2shape[0] + r1r2shape[1])) {
      ka = Kshape[0][0];
      kb = Kshape[0][1];
      kc = Kshape[0][2];
      kd = Kshape[0][3];
    } else if (radius_ratio < (0.5 * r1r2shape[1] + r1r2shape[2])) {
      ka = Kshape[1][0];
      kb = Kshape[1][1];
      kc = Kshape[1][2];
      kd = Kshape[1][3];
    } else if (radius_ratio < (0.5 * r1r2shape[2] + r1r2shape[3])) {
      ka = Kshape[2][0];
      kb = Kshape[2][1];
      kc = Kshape[2][2];
      kd = Kshape[2][3];
    } else if (radius_ratio < (0.5 * r1r2shape[3] + r1r2shape[4])) {
      ka = Kshape[3][0];
      kb = Kshape[3][1];
      kc = Kshape[3][2];
      kd = Kshape[3][3];
    } else if (radius_ratio < (0.5 * r1r2shape[4] + r1r2shape[5])) {
      ka = Kshape[4][0];
      kb = Kshape[4][1];
      kc = Kshape[4][2];
      kd = Kshape[4][3];
    } else if (radius_ratio < (0.5 * r1r2shape[5] + r1r2shape[6])) {
      ka = Kshape[5][0];
      kb = Kshape[5][1];
      kc = Kshape[5][2];
      kd = Kshape[5][3];
    } else {
      ka = Kshape[6][0];
      kb = Kshape[6][1];
      kc = Kshape[6][2];
      kd = Kshape[6][3];
    }

  // Calculate the smoothed vertical velocity
  smooth_strength = 1.0 / (1 + pow(ka * abs(radius_ratio + kc), kb)) + kd * radius_ratio;
  if (smooth_strength  < 0) {
    smooth_strength = 0;
  }
  } else {
    smooth_strength = 0;
  }

  // Calculate downdraft velocity at teh edge of the updraft
  float down;
  if (distance > inner_rad and radius_ratio < 2) {
    down = 3.14159 / 6.0 * sin(3.14159 * radius_ratio);
  } else {
    down = 0;
  }

  float alt_down_scale;
  float down_strength;
  if (alt_ratio > 0.5 and alt_ratio <= 0.9) {
    alt_down_scale = 2.5 * (alt_ratio - 0.5);
    down_strength = alt_down_scale * down;
    if (down_strength > 0) {
      down_strength = 0;
    }
  } else {
    alt_down_scale = 0;
    down_strength = 0;
  }

  float intermediate_strength = smooth_strength * core_strength + down_strength * normalized_strength;

  // Calculate environmental sink velocity
  float updraft_area = num_thermals * 3.14159 * pow(outer_rad, 2);
  float total_area = thermalAreaWidth * thermalAreaHeight;
  float env_strength = 0;
  if (sflag) {
    env_strength = -(updraft_area * thermal_strength * (1-alt_down_scale))/(total_area-updraft_area);
    if (env_strength > 0) {
      env_strength = 0;
    }
  }

  // Stretch the updraft to blend with the sink at the edge
  float final_strength = 0;
  if (distance > inner_rad) {
    final_strength = intermediate_strength * (1 - env_strength/core_strength) + env_strength;
  } else {
    final_strength = env_strength;
  }

  return final_strength;
}

// DumpThermalInfo takes all the information for individual thermals and makes it into a comma 
// seperated string to be sent to an output. This allows an outside program to plot the thermals
std::string FGWinds::DumpThermalInfo()
{

  std::string output;

  output = output + to_string(numThermals);
  for (int i = 0; i < numThermals; i++) {
    output = output + "," + to_string(thermalStrengths[i]) + "," 
                    + to_string(thermalHeights[i]) + ","
                    + to_string(thermalLocations[i](1)) + ","
                    + to_string(thermalLocations[i](2));

  }

  return output;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGWinds::bind(void)
{
  typedef double (FGWinds::*PMF)(int) const;
  typedef int (FGWinds::*PMFt)(void) const;
  typedef void   (FGWinds::*PMFd)(int,double);
  typedef void   (FGWinds::*PMFi)(int);
  typedef double (FGWinds::*Ptr)(void) const;

  // User-specified steady, constant, wind properties (local navigational/geographic frame: N-E-D)
  PropertyManager->Tie("atmosphere/psiw-rad", this, &FGWinds::GetWindPsi, &FGWinds::SetWindPsi);
  PropertyManager->Tie("atmosphere/wind-north-fps", this, eNorth, (PMF)&FGWinds::GetWindNED,
                                                          (PMFd)&FGWinds::SetWindNED);
  PropertyManager->Tie("atmosphere/wind-east-fps",  this, eEast, (PMF)&FGWinds::GetWindNED,
                                                          (PMFd)&FGWinds::SetWindNED);
  PropertyManager->Tie("atmosphere/wind-down-fps",  this, eDown, (PMF)&FGWinds::GetWindNED,
                                                          (PMFd)&FGWinds::SetWindNED);
  PropertyManager->Tie("atmosphere/wind-mag-fps", this, &FGWinds::GetWindspeed,
                                                        &FGWinds::SetWindspeed);

  // User-specifieded gust (local navigational/geographic frame: N-E-D)
  PropertyManager->Tie("atmosphere/gust-north-fps", this, eNorth, (PMF)&FGWinds::GetGustNED,
                                                          (PMFd)&FGWinds::SetGustNED);
  PropertyManager->Tie("atmosphere/gust-east-fps",  this, eEast, (PMF)&FGWinds::GetGustNED,
                                                          (PMFd)&FGWinds::SetGustNED);
  PropertyManager->Tie("atmosphere/gust-down-fps",  this, eDown, (PMF)&FGWinds::GetGustNED,
                                                          (PMFd)&FGWinds::SetGustNED);

  // User-specified 1 - cosine gust parameters (in specified frame)
  PropertyManager->Tie("atmosphere/cosine-gust/startup-duration-sec", this, (Ptr)0L, &FGWinds::StartupGustDuration);
  PropertyManager->Tie("atmosphere/cosine-gust/steady-duration-sec", this, (Ptr)0L, &FGWinds::SteadyGustDuration);
  PropertyManager->Tie("atmosphere/cosine-gust/end-duration-sec", this, (Ptr)0L, &FGWinds::EndGustDuration);
  PropertyManager->Tie("atmosphere/cosine-gust/magnitude-ft_sec", this, (Ptr)0L, &FGWinds::GustMagnitude);
  PropertyManager->Tie("atmosphere/cosine-gust/frame", this, (PMFt)0L, (PMFi)&FGWinds::GustFrame);
  PropertyManager->Tie("atmosphere/cosine-gust/X-velocity-ft_sec", this, (Ptr)0L, &FGWinds::GustXComponent);
  PropertyManager->Tie("atmosphere/cosine-gust/Y-velocity-ft_sec", this, (Ptr)0L, &FGWinds::GustYComponent);
  PropertyManager->Tie("atmosphere/cosine-gust/Z-velocity-ft_sec", this, (Ptr)0L, &FGWinds::GustZComponent);
  PropertyManager->Tie("atmosphere/cosine-gust/start", this, static_cast<bool (FGWinds::*)(void) const>(nullptr), &FGWinds::StartGust);

  // User-specified turbulence (local navigational/geographic frame: N-E-D)
  PropertyManager->Tie("atmosphere/turb-north-fps", this, eNorth, (PMF)&FGWinds::GetTurbNED,
                                                          (PMFd)&FGWinds::SetTurbNED);
  PropertyManager->Tie("atmosphere/turb-east-fps",  this, eEast, (PMF)&FGWinds::GetTurbNED,
                                                          (PMFd)&FGWinds::SetTurbNED);
  PropertyManager->Tie("atmosphere/turb-down-fps",  this, eDown, (PMF)&FGWinds::GetTurbNED,
                                                          (PMFd)&FGWinds::SetTurbNED);
  // Experimental turbulence parameters
  PropertyManager->Tie("atmosphere/p-turb-rad_sec", this,1, (PMF)&FGWinds::GetTurbPQR);
  PropertyManager->Tie("atmosphere/q-turb-rad_sec", this,2, (PMF)&FGWinds::GetTurbPQR);
  PropertyManager->Tie("atmosphere/r-turb-rad_sec", this,3, (PMF)&FGWinds::GetTurbPQR);
  PropertyManager->Tie("atmosphere/turb-type", this, (PMFt)&FGWinds::GetTurbType, (PMFi)&FGWinds::SetTurbType);
  PropertyManager->Tie("atmosphere/turb-rate", this, &FGWinds::GetTurbRate, &FGWinds::SetTurbRate);
  PropertyManager->Tie("atmosphere/turb-gain", this, &FGWinds::GetTurbGain, &FGWinds::SetTurbGain);
  PropertyManager->Tie("atmosphere/turb-rhythmicity", this, &FGWinds::GetRhythmicity,
                                                            &FGWinds::SetRhythmicity);

  // Parameters for milspec turbulence
  PropertyManager->Tie("atmosphere/turbulence/milspec/windspeed_at_20ft_AGL-fps",
                       this, &FGWinds::GetWindspeed20ft,
                             &FGWinds::SetWindspeed20ft);
  PropertyManager->Tie("atmosphere/turbulence/milspec/severity",
                       this, &FGWinds::GetProbabilityOfExceedence,
                             &FGWinds::SetProbabilityOfExceedence);

  // Total, calculated winds (local navigational/geographic frame: N-E-D). Read only.
  PropertyManager->Tie("atmosphere/total-wind-north-fps", this, eNorth, (PMF)&FGWinds::GetTotalWindNED);
  PropertyManager->Tie("atmosphere/total-wind-east-fps",  this, eEast,  (PMF)&FGWinds::GetTotalWindNED);
  PropertyManager->Tie("atmosphere/total-wind-down-fps",  this, eDown,  (PMF)&FGWinds::GetTotalWindNED);

  // Parameters for thermal model
  PropertyManager->Tie("atmosphere/thermal_conv_velo_scale", this, &FGWinds::GetConvVeloScale, &FGWinds::SetConvVeloScale);
  PropertyManager->Tie("atmosphere/thermal_conv_velo_scale_std", this, &FGWinds::GetConvVeloScaleSTD, &FGWinds::SetConvVeloScaleSTD);
  PropertyManager->Tie("atmosphere/thermal_conv_layer_thickness", this, &FGWinds::GetConvLayerThickness, &FGWinds::SetConvLayerThickness);
  PropertyManager->Tie("atmosphere/thermal_conv_layer_thickness_std", this, &FGWinds::GetConvLayerThicknessSTD, &FGWinds::SetConvLayerThicknessSTD);
  PropertyManager->Tie("atmosphere/thermal_area_width", this, &FGWinds::GetThermalAreaWidth, &FGWinds::SetThermalAreaWidth);
  PropertyManager->Tie("atmosphere/thermal_area_height", this, &FGWinds::GetThermalAreaHeight, &FGWinds::SetThermalAreaHeight);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//    The bitmasked value choices are as follows:
//    unset: In this case (the default) JSBSim would only print
//       out the normally expected messages, essentially echoing
//       the config files as they are read. If the environment
//       variable is not set, debug_lvl is set to 1 internally
//    0: This requests JSBSim not to output any messages
//       whatsoever.
//    1: This value explicity requests the normal JSBSim
//       startup messages
//    2: This value asks for a message to be printed out when
//       a class is instantiated
//    4: When this value is set, a message is displayed when a
//       FGModel object executes its Run() method
//    8: When this value is set, various runtime state variables
//       are printed out periodically
//    16: When set various parameters are sanity checked and
//       a message is printed out when they go out of bounds

void FGWinds::Debug(int from)
{
  if (debug_lvl <= 0) return;

  if (debug_lvl & 1) { // Standard console startup message output
    if (from == 0) { // Constructor
    }
  }
  if (debug_lvl & 2 ) { // Instantiation/Destruction notification
    if (from == 0) cout << "Instantiated: FGWinds" << endl;
    if (from == 1) cout << "Destroyed:    FGWinds" << endl;
  }
  if (debug_lvl & 4 ) { // Run() method entry print for FGModel-derived objects
  }
  if (debug_lvl & 8 ) { // Runtime state variables
  }
  if (debug_lvl & 16) { // Sanity checking
  }
  if (debug_lvl & 128) { //
  }
  if (debug_lvl & 64) {
    if (from == 0) { // Constructor
    }
  }
}

} // namespace JSBSim
