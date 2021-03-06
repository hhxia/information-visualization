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

/* Orestis Malaspinas contributed this code.
 */

/** \file
 * A collection of dynamics classes (e.g. BGK) with which a Cell object
 * can be instantiated -- generic implementation.
 */
#ifndef ENTROPIC_LB_DYNAMICS_HH
#define ENTROPIC_LB_DYNAMICS_HH

#include "latticeBoltzmann/dynamicsTemplates.h"
#include "latticeBoltzmann/momentTemplates.h"
#include "latticeBoltzmann/externalForceTemplates.h"
#include "latticeBoltzmann/entropicLbTemplates.h"
#include "complexDynamics/entropicDynamics.h"
#include "core/latticeStatistics.h"
#include <algorithm>
#include <limits>

namespace plb {

//==============================================================================//
/////////////////////////// Class EntropicDynamics ///////////////////////////////
//==============================================================================//
/** \param omega_ relaxation parameter, related to the dynamic viscosity
 *  \param moments_ a Moments object to know how to compute velocity moments
 */
template<typename T, template<typename U> class Descriptor>
EntropicDynamics<T,Descriptor>::EntropicDynamics(T omega_)
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
EntropicDynamics<T,Descriptor>* EntropicDynamics<T,Descriptor>::clone() const {
    return new EntropicDynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
T EntropicDynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j, T jSqr, T thetaBar ) const
{
    T rho = Descriptor<T>::fullRho(rhoBar);
    T invRho = Descriptor<T>::invRho(rhoBar);
    Array<T,Descriptor<T>::d> u;
    for (int iD=0; iD<Descriptor<T>::d; ++iD) {
        u[iD] = j[iD] * invRho;
    }
    return entropicLbTemplates<T,Descriptor>::equilibrium(iPop, rho, u);
}

template<typename T, template<typename U> class Descriptor>
void EntropicDynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    typedef Descriptor<T> L;
    typedef entropicLbTemplates<T,Descriptor> eLbTempl;
    
    T rho;
    Array<T,Descriptor<T>::d> u;
    momentTemplates<T,Descriptor>::compute_rho_uLb(cell, rho, u);
    T uSqr = VectorTemplate<T,Descriptor>::normSqr(u);
    
    Array<T,Descriptor<T>::q> f, fEq, fNeq;
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        fEq[iPop]  = eLbTempl::equilibrium(iPop,rho,u);
        fNeq[iPop] = cell[iPop] - fEq[iPop];
        f[iPop]    = cell[iPop] + L::t[iPop];
        fEq[iPop] += L::t[iPop];
    }
    //==============================================================================//
    //============= Evaluation of alpha using a Newton Raphson algorithm ===========//
    //==============================================================================//

    T alpha = (T)2;
    bool converged = getAlpha(alpha,f,fNeq);
    if (!converged)
    {
        exit(1);
    }
    
    PLB_ASSERT(converged);
    
    T omegaTot = this->getOmega() / (T)2 * alpha;
    for (plint iPop=0; iPop < Descriptor<T>::q; ++iPop) 
    {
        cell[iPop] *= (T)1-omegaTot;
        cell[iPop] += omegaTot * (fEq[iPop]-L::t[iPop]);
    }
    
    if (cell.takesStatistics()) {
        gatherStatistics(statistics, Descriptor<T>::rhoBar(rho), uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T EntropicDynamics<T,Descriptor>::computeEntropy(Array<T,Descriptor<T>::q> const& f)
{
    typedef Descriptor<T> L;
    T entropy = T();
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        PLB_ASSERT(f[iPop] > T());
        entropy += f[iPop]*log(f[iPop]/L::t[iPop]);
    }

    return entropy;
}

template<typename T, template<typename U> class Descriptor>
T EntropicDynamics<T,Descriptor>::computeEntropyGrowth(Array<T,Descriptor<T>::q> const& f, Array<T,Descriptor<T>::q> const& fNeq, T alpha)
{
    typedef Descriptor<T> L;
    
    Array<T,Descriptor<T>::q> fAlphaFneq;
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        fAlphaFneq[iPop] = f[iPop] - alpha*fNeq[iPop];
    }

    return computeEntropy(f) - computeEntropy(fAlphaFneq);
}

template<typename T, template<typename U> class Descriptor>
T EntropicDynamics<T,Descriptor>::computeEntropyGrowthDerivative(Array<T,Descriptor<T>::q> const& f, Array<T,Descriptor<T>::q> const& fNeq, T alpha)
{
    typedef Descriptor<T> L;
    
    T entropyGrowthDerivative = T();
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        T tmp = f[iPop] - alpha*fNeq[iPop];
        PLB_ASSERT(tmp > T());
        entropyGrowthDerivative += fNeq[iPop]*(log(tmp/L::t[iPop]));
    }

    return entropyGrowthDerivative;
}

template<typename T, template<typename U> class Descriptor>
bool EntropicDynamics<T,Descriptor>::getAlpha(T &alpha, Array<T,Descriptor<T>::q> const& f, Array<T,Descriptor<T>::q> const& fNeq)
{
    const T epsilon = std::numeric_limits<T>::epsilon();

    T alphaGuess = T();
    const T var = 100.0;
    const T errorMax = epsilon*var;
    T error = 1.0;
    plint count = 0;
    for (count = 0; count < 10000; ++count)
    {
        T entGrowth = computeEntropyGrowth(f,fNeq,alpha);
        T entGrowthDerivative = computeEntropyGrowthDerivative(f,fNeq,alpha);
        if ((error < errorMax) || (fabs(entGrowth) < var*epsilon))
        {
            return true;
        }
        alphaGuess = alpha - entGrowth / entGrowthDerivative;
        error = fabs(alpha-alphaGuess);
        alpha = alphaGuess;
    }
    return false;
}

//====================================================================//
//////////////////// Class ForcedEntropicDynamics //////////////////////
//====================================================================//

/** \param omega_ relaxation parameter, related to the dynamic viscosity
 */
template<typename T, template<typename U> class Descriptor>
ForcedEntropicDynamics<T,Descriptor>::ForcedEntropicDynamics(T omega_)
    : IsoThermalBulkDynamics<T,Descriptor>(omega_)
{ }

template<typename T, template<typename U> class Descriptor>
ForcedEntropicDynamics<T,Descriptor>* ForcedEntropicDynamics<T,Descriptor>::clone() const {
    return new ForcedEntropicDynamics<T,Descriptor>(*this);
}

template<typename T, template<typename U> class Descriptor>
T ForcedEntropicDynamics<T,Descriptor>::computeEquilibrium (
        plint iPop, T rhoBar, Array<T,Descriptor<T>::d> const& j, T jSqr, T thetaBar ) const
{
    T rho = Descriptor<T>::fullRho(rhoBar);
    T invRho = Descriptor<T>::invRho(rhoBar);
    T uSqr = jSqr*invRho*invRho;
    Array<T,Descriptor<T>::d> u;
    for (int iD=0; iD<Descriptor<T>::d; ++iD) {
        u[iD] = j[iD] * invRho;
    }
    return entropicLbTemplates<T,Descriptor>::equilibrium(iPop, rho, u);
}

template<typename T, template<typename U> class Descriptor>
void ForcedEntropicDynamics<T,Descriptor>::collide (
        Cell<T,Descriptor>& cell,
        BlockStatistics<T>& statistics )
{
    typedef Descriptor<T> L;
    typedef entropicLbTemplates<T,Descriptor> eLbTempl;

    T rho;
    Array<T,Descriptor<T>::d> u;
    momentTemplates<T,Descriptor>::compute_rho_uLb(cell, rho, u);
    T uSqr = VectorTemplate<T,Descriptor>::normSqr(u);

    Array<T,Descriptor<T>::q> f, fEq, fNeq;
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        fEq[iPop]  = eLbTempl::equilibrium(iPop,rho,u);
        fNeq[iPop] = cell[iPop] - fEq[iPop];
        f[iPop]    = cell[iPop] + L::t[iPop];
        fEq[iPop] += L::t[iPop];
    }
    //==============================================================================//
    //============= Evaluation of alpha using a Newton Raphson algorithm ===========//
    //==============================================================================//

    T alpha = (T)2;
    bool converged = getAlpha(alpha,f,fNeq);
    if (!converged)
    {
        exit(1);
    }

    PLB_ASSERT(converged);
    
    T* force = cell.getExternal(forceBeginsAt);
    for (int iDim=0; iDim<Descriptor<T>::d; ++iDim)
    {
        u[iDim] += force[iDim] / (T)2.;
    }
    uSqr = VectorTemplate<T,Descriptor>::normSqr(u);
    T omegaTot = this->getOmega() / (T)2 * alpha;
    for (plint iPop=0; iPop < Descriptor<T>::q; ++iPop) 
    {
        cell[iPop] *= (T)1-omegaTot;
        cell[iPop] += omegaTot * eLbTempl::equilibrium(iPop,rho,u);
    }
    externalForceTemplates<T,Descriptor>::addGuoForce(cell, u, omegaTot);
    
    if (cell.takesStatistics())
    {
        gatherStatistics(statistics, Descriptor<T>::rhoBar(rho), uSqr);
    }
}

template<typename T, template<typename U> class Descriptor>
T ForcedEntropicDynamics<T,Descriptor>::computeEntropy(Array<T,Descriptor<T>::q> const& f)
{
    typedef Descriptor<T> L;
    T entropy = T();
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        PLB_ASSERT(f[iPop] > T());
        entropy += f[iPop]*log(f[iPop]/L::t[iPop]);
    }

    return entropy;
}

template<typename T, template<typename U> class Descriptor>
T ForcedEntropicDynamics<T,Descriptor>::computeEntropyGrowth(Array<T,Descriptor<T>::q> const& f, Array<T,Descriptor<T>::q> const& fNeq, T alpha)
{
    typedef Descriptor<T> L;
    
    Array<T,Descriptor<T>::q> fAlphaFneq;
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        fAlphaFneq[iPop] = f[iPop] - alpha*fNeq[iPop];
    }

    return computeEntropy(f) - computeEntropy(fAlphaFneq);
}

template<typename T, template<typename U> class Descriptor>
T ForcedEntropicDynamics<T,Descriptor>::computeEntropyGrowthDerivative(Array<T,Descriptor<T>::q> const& f, Array<T,Descriptor<T>::q> const& fNeq, T alpha)
{
    typedef Descriptor<T> L;
    
    T entropyGrowthDerivative = T();
    for (plint iPop = 0; iPop < L::q; ++iPop)
    {
        T tmp = f[iPop] - alpha*fNeq[iPop];
        PLB_ASSERT(tmp > T());
        entropyGrowthDerivative += fNeq[iPop]*log(tmp/L::t[iPop]);
    }

    return entropyGrowthDerivative;
}

template<typename T, template<typename U> class Descriptor>
bool ForcedEntropicDynamics<T,Descriptor>::getAlpha(T& alpha, Array<T,Descriptor<T>::q> const& f, Array<T,Descriptor<T>::q> const& fNeq)
{
    const T epsilon = std::numeric_limits<T>::epsilon();

    T alphaGuess = T();
    const T var = 100.0;
    const T errorMax = epsilon*var;
    T error = 1.0;
    plint count = 0;
    for (count = 0; count < 10000; ++count)
    {
        T entGrowth = computeEntropyGrowth(f,fNeq,alpha);
        T entGrowthDerivative = computeEntropyGrowthDerivative(f,fNeq,alpha);
        if ((error < errorMax) || (fabs(entGrowth) < var*epsilon))
        {
            return true;
        }
        alphaGuess = alpha - entGrowth / entGrowthDerivative;
        error = fabs(alpha-alphaGuess);
        alpha = alphaGuess;
    }
    return false;
}

}

#endif
