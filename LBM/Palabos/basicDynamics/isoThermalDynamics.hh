/* This file is part of the Palabos library.
 * Copyright (C) 2009 Jonas Latt
 * E-mail contact: jonas@lbmethod.org
 * The most recent release of Palabos can be downloaded at 
 * <http://www.lbmethod.org/palabos/>
 *
 * The library Palabos is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * The library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/** \file
 * A collection of dynamics classes (e.g. BGK) with which a Cell object
 * can be instantiated -- generic implementation.
 */
#ifndef ISO_THERMAL_DYNAMICS_HH
#define ISO_THERMAL_DYNAMICS_HH

#include "basicDynamics/isoThermalDynamics.h"
#include "core/cell.h"
#include "latticeBoltzmann/dynamicsTemplates.h"
#include "latticeBoltzmann/momentTemplates.h"
#include "latticeBoltzmann/externalForceTemplates.h"
#include "latticeBoltzmann/offEquilibriumTemplates.h"
#include "latticeBoltzmann/d3q13Templates.h"
#include "latticeBoltzmann/geometricOperationTemplates.h"
#include "core/latticeStatistics.h"
#include <algorithm>
#include <limits>

namespace plb {

/* *************** Class IsoThermalBulkDynamics ************************************ */

template<typename T, template<typename U> class Descriptor>
IsoThermalBulkDynamics<T,Descriptor>::IsoThermalBulkDynamics(T omega_)
  : BasicBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::regularize (
         Cell<T,Descriptor>& cell, T rhoBar, Array<T,Descriptor<T>::d> const& j,
         T jSqr, Array<T,SymmetricTensor<T,Descriptor>::n> const& PiNeq, T thetaBar ) const
{
    typedef Descriptor<T> L;
    cell[0] = this->computeEquilibrium(0, rhoBar, j, jSqr)
                + offEquilibriumTemplates<T,Descriptor>::fromPiToFneq(0, PiNeq);
    for (plint iPop=1; iPop<=L::q/2; ++iPop) {
        cell[iPop] = this->computeEquilibrium(iPop, rhoBar, j, jSqr);
        cell[iPop+L::q/2] = this->computeEquilibrium(iPop+L::q/2, rhoBar, j, jSqr);
        T fNeq = offEquilibriumTemplates<T,Descriptor>::fromPiToFneq(iPop, PiNeq);
        cell[iPop] += fNeq;
        cell[iPop+L::q/2] += fNeq;
    }
}

template<typename T, template<typename U> class Descriptor>
T IsoThermalBulkDynamics<T,Descriptor>::computeTemperature(Cell<T,Descriptor> const& cell) const
{
    return (T)1;
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::computeDeviatoricStress (
    Cell<T,Descriptor> const& cell, Array<T,SymmetricTensor<T,Descriptor>::n>& PiNeq ) const
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    momentTemplates<T,Descriptor>::get_rhoBar_j(cell, rhoBar, j);
    momentTemplates<T,Descriptor>::compute_PiNeq(cell, rhoBar, j, PiNeq);
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::computeHeatFlux (
        Cell<T,Descriptor> const& cell, Array<T,Descriptor<T>::d>& q ) const
{
    q.resetToZero();
}

template<typename T, template<typename U> class Descriptor>
T IsoThermalBulkDynamics<T,Descriptor>::computeEbar(Cell<T,Descriptor> const& cell) const
{
    return T();
}


template<typename T, template<typename U> class Descriptor>
plint IsoThermalBulkDynamics<T,Descriptor>::numDecomposedVariables(plint order) const {
    // Start with the decomposed version of the populations.
    plint numVariables =
                         // Order 0: density + velocity + fNeq
        ( order == 0 ) ? ( 1 + Descriptor<T>::d + Descriptor<T>::q )
                         // Order >=1: density + velocity + PiNeq
                       : ( 1 + Descriptor<T>::d + SymmetricTensor<T,Descriptor>::n );

    // Add the variables in the external scalars.
    numVariables += Descriptor<T>::ExternalField::numScalars;
    return numVariables;
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::decompose (
        Cell<T,Descriptor> const& cell, std::vector<T>& rawData, plint order ) const
{
    rawData.resize(numDecomposedVariables(order));

    if (order==0) {
        decomposeOrder0(cell, rawData);
    }
    else {
        decomposeOrder1(cell, rawData);
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::recompose (
        Cell<T,Descriptor>& cell, std::vector<T> const& rawData, plint order ) const
{
    PLB_PRECONDITION( (plint)rawData.size() == numDecomposedVariables(order) );

    if (order==0) {
        recomposeOrder0(cell, rawData);
    }
    else {
        recomposeOrder1(cell, rawData);
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::rescale (
        std::vector<T>& rawData, T xDxInv, T xDt, plint order ) const
{
    PLB_PRECONDITION( (plint)rawData.size()==numDecomposedVariables(order) );

    if (order==0) {
        rescaleOrder0(rawData, xDxInv, xDt);
    }
    else {
        rescaleOrder1(rawData, xDxInv, xDt);
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::decomposeOrder0 (
        Cell<T,Descriptor> const& cell, std::vector<T>& rawData ) const
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    momentTemplates<T,Descriptor>::get_rhoBar_j(cell, rhoBar, j);
    T jSqr = VectorTemplate<T,Descriptor>::normSqr(j);
    rawData[0] = rhoBar;
    j.to_cArray(&rawData[1]);

    for (plint iPop=0; iPop<Descriptor<T>::q; ++iPop) {
        rawData[1+Descriptor<T>::d+iPop] =
            cell[iPop] - this->computeEquilibrium(iPop, rhoBar, j, jSqr);
    }

    int offset = 1+Descriptor<T>::d+Descriptor<T>::q;
    for (plint iExt=0; iExt<Descriptor<T>::ExternalField::numScalars; ++iExt) {
        rawData[offset+iExt] = *cell.getExternal(iExt);
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::decomposeOrder1 (
        Cell<T,Descriptor> const& cell, std::vector<T>& rawData ) const
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    Array<T,SymmetricTensor<T,Descriptor>::n> PiNeq;
    momentTemplates<T,Descriptor>::compute_rhoBar_j_PiNeq(cell, rhoBar, j, PiNeq);

    rawData[0] = rhoBar;
    j.to_cArray(&rawData[1]);
    PiNeq.to_cArray(&rawData[1+Descriptor<T>::d]);

    int offset = 1+Descriptor<T>::d+SymmetricTensor<T,Descriptor>::n;
    for (plint iExt=0; iExt<Descriptor<T>::ExternalField::numScalars; ++iExt) {
        rawData[offset+iExt] = *cell.getExternal(iExt);
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::recomposeOrder0 (
        Cell<T,Descriptor>& cell, std::vector<T> const& rawData ) const
{
    T rhoBar = rawData[0];
    Array<T,Descriptor<T>::d> j;
    j.from_cArray(&rawData[1]);
    T jSqr = VectorTemplate<T,Descriptor>::normSqr(j);

    for (plint iPop=0; iPop<Descriptor<T>::q; ++iPop) {
        cell[iPop] = this->computeEquilibrium(iPop, rhoBar, j, jSqr)
                      + rawData[1+Descriptor<T>::d+iPop];
    }

    int offset = 1+Descriptor<T>::d+Descriptor<T>::q;
    for (plint iExt=0; iExt<Descriptor<T>::ExternalField::numScalars; ++iExt) {
        *cell.getExternal(iExt) = rawData[offset+iExt];
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::recomposeOrder1 (
        Cell<T,Descriptor>& cell, std::vector<T> const& rawData ) const
{
    typedef Descriptor<T> L;

    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    Array<T,SymmetricTensor<T,Descriptor>::n> PiNeq;

    rhoBar = rawData[0];
    j.from_cArray(&rawData[1]);
    T jSqr = VectorTemplate<T,Descriptor>::normSqr(j);
    PiNeq.from_cArray(&rawData[1+Descriptor<T>::d]);

    cell[0] = this->computeEquilibrium(0, rhoBar, j, jSqr)
                + offEquilibriumTemplates<T,Descriptor>::fromPiToFneq(0, PiNeq);
    for (plint iPop=1; iPop<=L::q/2; ++iPop) {
        cell[iPop] = this->computeEquilibrium(iPop, rhoBar, j, jSqr);
        cell[iPop+L::q/2] = this->computeEquilibrium(iPop+L::q/2, rhoBar, j, jSqr);
        T fNeq = offEquilibriumTemplates<T,Descriptor>::fromPiToFneq(iPop, PiNeq);
        cell[iPop] += fNeq;
        cell[iPop+L::q/2] += fNeq;
    }

    int offset = 1+Descriptor<T>::d+SymmetricTensor<T,Descriptor>::n;
    for (plint iExt=0; iExt<Descriptor<T>::ExternalField::numScalars; ++iExt) {
        *cell.getExternal(iExt) = rawData[offset+iExt];
    }
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::rescaleOrder0 (
        std::vector<T>& rawData, T xDxInv, T xDt ) const
{
    // Don't change rho (rawData[0]), because it is invariant

    // Change velocity, according to its units dx/dt
    T velScale = xDt * xDxInv;
    for (plint iVel=0; iVel<Descriptor<T>::d; ++iVel) {
        rawData[1+iVel] *= velScale;
    }

    // Change off-equilibrium, according to its units 1/dt
    T fNeqScale = xDt;
    for (plint iFneq=0; iFneq<Descriptor<T>::q; ++iFneq) {
        rawData[1+Descriptor<T>::d+iFneq] *= fNeqScale;
    }

    // Don't change external fields; their scaling must be taken care of
    //   in specialized versions of this class.
}

template<typename T, template<typename U> class Descriptor>
void IsoThermalBulkDynamics<T,Descriptor>::rescaleOrder1 (
        std::vector<T>& rawData, T xDxInv, T xDt ) const
{
    // Don't change rho (rawData[0]), because it is invariant

    // Change velocity, according to its units dx/dt
    T velScale = xDt * xDxInv;
    for (plint iVel=0; iVel<Descriptor<T>::d; ++iVel) {
        rawData[1+iVel] *= velScale;
    }

    // Change off-equilibrium stress, according to its units 1/dt
    T PiNeqScale = xDt;
    for (plint iPi=0; iPi<SymmetricTensor<T,Descriptor>::n; ++iPi) {
        rawData[1+Descriptor<T>::d+iPi] *= PiNeqScale;
    }

    // Don't change external fields; their scaling must be taken care of
    //   in specialized versions of this class.
}

/* *************** Class BGKdynamics *********************************************** */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
BGKdynamics<T,Descriptor>::BGKdynamics(T omega_ )
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
BGKdynamics<T,Descriptor>* BGKdynamics<T,Descriptor>::clone() const {
    return new BGKdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void BGKdynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    momentTemplates<T,Descriptor>::get_rhoBar_j(cell, rhoBar, j);
    T uSqr = dynamicsTemplates<T,Descriptor>::bgk_ma2_collision(cell, rhoBar, j, this->getOmega());
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar, uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T BGKdynamics<T,Descriptor>::computeEquilibrium(plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j,
                                                T jSqr, T thetaBar) const
{
    T invRho = Descriptor<T>::invRho(rhoBar);
    return dynamicsTemplates<T,Descriptor>::bgk_ma2_equilibrium(iPop, rhoBar, invRho, j, jSqr);
}


/* *************** Class ExternalMomentBGKdynamics ********************************** */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
ExternalMomentBGKdynamics<T,Descriptor>::ExternalMomentBGKdynamics(T omega_ )
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
ExternalMomentBGKdynamics<T,Descriptor>* ExternalMomentBGKdynamics<T,Descriptor>::clone() const {
    return new ExternalMomentBGKdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void ExternalMomentBGKdynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T& rho   = *cell.getExternal(Descriptor<T>::ExternalField::densityBeginsAt);
    T rhoBar = Descriptor<T>::rhoBar(rho);
    Array<T,Descriptor<T>::d> j;
    j.from_cArray( cell.getExternal(Descriptor<T>::ExternalField::momentumBeginsAt) );
    T uSqr = dynamicsTemplates<T,Descriptor>::bgk_ma2_collision(cell, rhoBar, j, this->getOmega());
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar, uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T ExternalMomentBGKdynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j,
        T jSqr, T thetaBar) const
{
    T invRho = Descriptor<T>::invRho(rhoBar);
    return dynamicsTemplates<T,Descriptor>::bgk_ma2_equilibrium(iPop, rhoBar, invRho, j, jSqr);
}


/* *************** Class IncBGKdynamics ******************************************** */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
IncBGKdynamics<T,Descriptor>::IncBGKdynamics(T omega_)
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
IncBGKdynamics<T,Descriptor>* IncBGKdynamics<T,Descriptor>::clone() const {
    return new IncBGKdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void IncBGKdynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    momentTemplates<T,Descriptor>::get_rhoBar_j(cell, rhoBar, j);
    T uSqr = dynamicsTemplates<T,Descriptor>::bgk_inc_collision(cell, rhoBar, j, this->getOmega());
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar, uSqr);
    }
}


template<typename T, template<typename U> class Descriptor>
T IncBGKdynamics<T,Descriptor>::computeEquilibrium(plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j,
                                                   T jSqr, T thetaBar) const
{
    // For the incompressible BGK dynamics, the "1/rho" pre-factor of
    // the O(Ma^2) term is unity.
    T invRho = (T)1;
    return dynamicsTemplates<T,Descriptor>::bgk_ma2_equilibrium(iPop, rhoBar, invRho, j, jSqr);
}


/* *************** Class ConstRhoBGKdynamics *************************************** */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
ConstRhoBGKdynamics<T,Descriptor>::ConstRhoBGKdynamics(T omega_)
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
ConstRhoBGKdynamics<T,Descriptor>* ConstRhoBGKdynamics<T,Descriptor>::clone()
    const
{
    return new ConstRhoBGKdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void ConstRhoBGKdynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    momentTemplates<T,Descriptor>::get_rhoBar_j(cell, rhoBar, j);
    T rho = Descriptor<T>::fullRho(rhoBar);

    T deltaRho = -statistics.getAverage(LatticeStatistics::avRhoBar)
                   + (1-Descriptor<T>::SkordosFactor());
    T ratioRho = (T)1 + deltaRho/rho;

    T uSqr = dynamicsTemplates<T,Descriptor>::bgk_ma2_constRho_collision (
                cell, rhoBar, j, ratioRho, this->getOmega() );
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar+deltaRho, uSqr);
    }
}


template<typename T, template<typename U> class Descriptor>
T ConstRhoBGKdynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j,
        T jSqr, T thetaBar) const
{
    T invRho = Descriptor<T>::invRho(rhoBar);
    return dynamicsTemplates<T,Descriptor>::bgk_ma2_equilibrium(iPop, rhoBar, invRho, j, jSqr);
}


/* *************** Class RLBdynamics *********************************************** */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
RLBdynamics<T,Descriptor>::RLBdynamics(Dynamics<T,Descriptor>* baseDynamics)
    : BulkCompositeDynamics<T,Descriptor>(baseDynamics)
{ }

template<typename T, template<typename U> class Descriptor>
RLBdynamics<T,Descriptor>* RLBdynamics<T,Descriptor>::clone() const
{
    return new RLBdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void RLBdynamics<T,Descriptor>::completePopulations(Cell<T,Descriptor>& cell) const
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    Array<T,SymmetricTensor<T,Descriptor>::n> PiNeq;
    momentTemplates<T,Descriptor>::compute_rhoBar_j_PiNeq(cell, rhoBar, j, PiNeq);
    T jSqr = VectorTemplate<T,Descriptor>::normSqr(j);
    for (plint iPop=0; iPop<Descriptor<T>::q; ++iPop) {
        cell[iPop] = this -> computeEquilibrium(iPop, rhoBar, j, jSqr)
                   + offEquilibriumTemplates<T,Descriptor>::fromPiToFneq(iPop, PiNeq);
    }
}


/* *************** Class RegularizedBGKdynamics ************************************ */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
RegularizedBGKdynamics<T,Descriptor>::RegularizedBGKdynamics(T omega_)
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
RegularizedBGKdynamics<T,Descriptor>* RegularizedBGKdynamics<T,Descriptor>::clone() const
{
    return new RegularizedBGKdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void RegularizedBGKdynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    Array<T,SymmetricTensor<T,Descriptor>::n> PiNeq;
    momentTemplates<T,Descriptor>::compute_rhoBar_j_PiNeq(cell, rhoBar, j, PiNeq);
    T uSqr = dynamicsTemplates<T,Descriptor>::rlb_collision (
                 cell, rhoBar, j, PiNeq, this->getOmega() );
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar, uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T RegularizedBGKdynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j, T jSqr, T thetaBar ) const
{
    T invRho = Descriptor<T>::invRho(rhoBar);
    return dynamicsTemplates<T,Descriptor>::bgk_ma2_equilibrium(iPop, rhoBar, invRho, j, jSqr);
}


/* *************** Class ExternalMomentRegularizedBGKdynamics *********************** */

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
ExternalMomentRegularizedBGKdynamics<T,Descriptor>::ExternalMomentRegularizedBGKdynamics(T omega_ )
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
ExternalMomentRegularizedBGKdynamics<T,Descriptor>* ExternalMomentRegularizedBGKdynamics<T,Descriptor>::clone() const {
    return new ExternalMomentRegularizedBGKdynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void ExternalMomentRegularizedBGKdynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T& rho   = *cell.getExternal(Descriptor<T>::ExternalField::densityBeginsAt);
    T rhoBar = Descriptor<T>::rhoBar(rho);
    Array<T,Descriptor<T>::d> j;
    Array<T,SymmetricTensor<T,Descriptor>::n> PiNeq;
    j.from_cArray( cell.getExternal(Descriptor<T>::ExternalField::momentumBeginsAt) );
    momentTemplates<T,Descriptor>::compute_PiNeq(cell, rhoBar, j, PiNeq);
    T uSqr = dynamicsTemplates<T,Descriptor>::rlb_collision (
                 cell, rhoBar, j, PiNeq, this->getOmega() );
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar, uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T ExternalMomentRegularizedBGKdynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j,
        T jSqr, T thetaBar) const
{
    T invRho = Descriptor<T>::invRho(rhoBar);
    return dynamicsTemplates<T,Descriptor>::bgk_ma2_equilibrium(iPop, rhoBar, invRho, j, jSqr);
}


/* *************** Class ChopardDynamics ************************************ */

/** \param vs2_ speed of sound
 *  \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
ChopardDynamics<T,Descriptor>::ChopardDynamics(T vs2_, T omega_)
    : IsoThermalBulkDynamics<T,Descriptor>(omega_),
      vs2(vs2_)
{ }

template<typename T, template<typename U> class Descriptor>
ChopardDynamics<T,Descriptor>* ChopardDynamics<T,Descriptor>::clone() const
{
    return new ChopardDynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
void ChopardDynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    T rhoBar;
    Array<T,Descriptor<T>::d> j;
    momentTemplates<T,Descriptor>::get_rhoBar_j(cell, rhoBar, j);
    T uSqr = chopardBgkCollision(cell, rhoBar, j, vs2, this->getOmega());
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, rhoBar, uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T ChopardDynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j, T jSqr, T thetaBar) const
{
    T invRho = Descriptor<T>::invRho(rhoBar);
    return chopardEquilibrium(iPop, rhoBar, invRho, j, jSqr, vs2);
}

template<typename T, template<typename U> class Descriptor>
T ChopardDynamics<T,Descriptor>::getParameter(plint whichParameter) const {
    switch (whichParameter) {
        case dynamicParams::omega_shear     : return this->getOmega();
        case dynamicParams::sqrSpeedOfSound : return this->getVs2();
    };
    return 0.;
}

template<typename T, template<typename U> class Descriptor>
void ChopardDynamics<T,Descriptor>::setParameter(plint whichParameter, T value) {
    switch (whichParameter) {
        case dynamicParams::omega_shear     : setOmega(value);
        case dynamicParams::sqrSpeedOfSound : setVs2(value);
    };
}

template<typename T, template<typename U> class Descriptor>
T ChopardDynamics<T,Descriptor>::getVs2() const {
    return vs2;
}

template<typename T, template<typename U> class Descriptor>
void ChopardDynamics<T,Descriptor>::setVs2(T vs2_) {
    vs2 = vs2_;
}

template<typename T, template<typename U> class Descriptor>
T ChopardDynamics<T,Descriptor>::chopardBgkCollision (
        Cell<T,Descriptor>& cell, T rhoBar, Array<T,Descriptor<T>::d> const& j, T vs2, T omega)
{
    const T jSqr = VectorTemplate<T,Descriptor>::normSqr(j);
    T invRho = Descriptor<T>::invRho(rhoBar);
    for (plint iPop=0; iPop < Descriptor<T>::q; ++iPop) {
        cell[iPop] *= (T)1-omega;
        cell[iPop] += omega * chopardEquilibrium(iPop, rhoBar, invRho, j, jSqr, vs2);
    }
    return invRho*invRho*jSqr;
}

template<typename T, template<typename U> class Descriptor>
T ChopardDynamics<T,Descriptor>::chopardEquilibrium (
        plint iPop, T rhoBar, T invRho, Array<T,Descriptor<T>::d> const& j, T jSqr, T vs2)
{
    T kappa = vs2 - Descriptor<T>::cs2;
    if (iPop==0) {
        return Descriptor<T>::invCs2 * (
                     kappa * (Descriptor<T>::t[0]-(T)1)
                   + rhoBar * (Descriptor<T>::t[0]*vs2-kappa)
                   - invRho * jSqr * Descriptor<T>::t[0]/(T)2*Descriptor<T>::invCs2 );
    }
    else {
        T c_j = T();
        for (int iD=0; iD < Descriptor<T>::d; ++iD) {
           c_j += Descriptor<T>::c[iPop][iD]*j[iD];
        }
        return Descriptor<T>::invCs2 * Descriptor<T>::t[iPop] * (
                     kappa
                   + rhoBar * vs2
                   + c_j
                   + invRho/(T)2 * (Descriptor<T>::invCs2*c_j - jSqr) );
    }
}

}

#endif
