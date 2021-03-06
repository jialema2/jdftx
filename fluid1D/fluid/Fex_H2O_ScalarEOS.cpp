/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman

This file is part of Fluid1D.

Fluid1D is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fluid1D is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fluid1D.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#include <fluid/Fex_H2O_ScalarEOS_internal.h>
#include <fluid/Fex_H2O_ScalarEOS.h>
#include <fluid/Fex_LJ.h>
#include <fluid/FluidMixture.h>
#include <core/Units.h>
#include <core/Operators.h>

static const double rOH = 1.0*Angstrom;
static const double thetaHOH = acos(-1.0/3);

Fex_H2O_ScalarEOS::Fex_H2O_ScalarEOS(FluidMixture& fluidMixture, bool polarizable)
: Fex(fluidMixture),
eval(new ScalarEOS_eval(T)),
propO(gInfo, eval->sphereRadius,0.0, 0.8476,&siteChargeKernel, true, 3.73*(polarizable?1:1e-6),&siteChargeKernel),
propH(gInfo, 0.0,0.0,-0.4238,&siteChargeKernel, true, 3.30*(polarizable?1:1e-6),&siteChargeKernel),
molecule("H2O",
	&propO,
		 vector3<>(0,0,0),
	&propH,
		 vector3<>(0, -rOH*sin(0.5*thetaHOH), rOH*cos(0.5*thetaHOH)),
		 vector3<>(0, +rOH*sin(0.5*thetaHOH), rOH*cos(0.5*thetaHOH)) )
{
	//Initialize the kernels:
	setQkernel(siteChargeKernel, gInfo, 1.385*Angstrom);
	setLJatt(fex_LJatt, gInfo, -9.0/(32*sqrt(2)*M_PI*pow(2*eval->sphereRadius,3)), 2*eval->sphereRadius);
}

Fex_H2O_ScalarEOS::~Fex_H2O_ScalarEOS()
{	delete eval;
}


double Fex_H2O_ScalarEOS::get_aDiel() const
{	return 1 - T/(7.35e3*Kelvin);
}

double Fex_H2O_ScalarEOS::compute(const ScalarFieldTilde* Ntilde, ScalarFieldTilde* grad_Ntilde) const
{	//Polarizability-averaged density:
	std::vector<double> ljWeights = getMolecule()->getLJweights();
	ScalarFieldTilde NavgTilde;
	for(unsigned i=0; i<ljWeights.size(); i++)
		NavgTilde += ljWeights[i] * Ntilde[i];
	//Compute LJatt weighted density:
	ScalarField Nbar = I(fex_LJatt * NavgTilde);
	//Evaluated weighted density functional:
	ScalarField Aex(&gInfo), AexPrime(&gInfo);
	serialLoop(eval, gInfo.S, Nbar.data(), Aex.data(), AexPrime.data());
	//Convert gradients:
	ScalarFieldTilde OJAex = O(J(Aex));
	for(unsigned i=0; i<ljWeights.size(); i++)
		grad_Ntilde[i] += ljWeights[i] * (fex_LJatt * Idag(Diag(AexPrime) * Jdag(O(NavgTilde))) + OJAex);
	return dot(NavgTilde, OJAex);
}

double Fex_H2O_ScalarEOS::computeUniform(const double* N, double* grad_N) const
{	//Polarizability-averaged density:
	std::vector<double> ljWeights = getMolecule()->getLJweights();
	double Navg = 0.;
	for(unsigned i=0; i<ljWeights.size(); i++)
		Navg += ljWeights[i] * N[i];
	//Evaluated weighted density functional:
	double AexPrime, Aex;
	(*eval)(0, &Navg, &Aex, &AexPrime);
	for(unsigned i=0; i<ljWeights.size(); i++)
		grad_N[i] += ljWeights[i] * (Aex + Navg*AexPrime);
	return Navg*Aex;
}

void Fex_H2O_ScalarEOS::directCorrelations(const double* N, ScalarFieldTildeCollection& C) const
{	//Polarizability-averaged density:
	std::vector<double> ljWeights = getMolecule()->getLJweights();
	double Navg = 0.;
	for(unsigned i=0; i<ljWeights.size(); i++)
		Navg += ljWeights[i] * N[i];
	//Compute upto second derivative of per-particle free energy:
	double AexPrime, Aex; (*eval)(0, &Navg, &Aex, &AexPrime);
	const double dN = Navg*1e-7;
	double AexPrimePlus,  AexPlus,  Nplus  = Navg+dN; (*eval)(0, &Nplus,  &AexPlus,  &AexPrimePlus);
	double AexPrimeMinus, AexMinus, Nminus = Navg-dN; (*eval)(0, &Nminus, &AexMinus, &AexPrimeMinus);
	double AexDblPrime = (AexPrimePlus - AexPrimeMinus) / (2*dN);
	//Accumulate correlations:
	ScalarFieldTilde fex_LJattTilde(fex_LJatt, gInfo); //A scalar field version of kernel to ease arithmetic
	for(unsigned i=0; i<ljWeights.size(); i++)
		for(unsigned j=i; j<ljWeights.size(); j++)
			C[fluidMixture.corrFuncIndex(i,j,this)] += (ljWeights[i]*ljWeights[j]) *  (2*AexPrime*fex_LJattTilde + Navg*AexDblPrime*(fex_LJatt*fex_LJattTilde));
}

double Fex_H2O_ScalarEOS::vdwRadius() const
{	return eval->vdwRadius();
}
