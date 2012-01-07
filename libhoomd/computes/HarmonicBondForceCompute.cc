/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2008-2011 Ames Laboratory
Iowa State University and The Regents of the University of Michigan All rights
reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

You may redistribute, use, and create derivate works of HOOMD-blue, in source
and binary forms, provided you abide by the following conditions:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer both in the code and
prominently in any materials provided with the distribution.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* All publications and presentations based on HOOMD-blue, including any reports
or published results obtained, in whole or in part, with HOOMD-blue, will
acknowledge its use according to the terms posted at the time of submission on:
http://codeblue.umich.edu/hoomd-blue/citations.html

* Any electronic documents citing HOOMD-Blue will link to the HOOMD-Blue website:
http://codeblue.umich.edu/hoomd-blue/

* Apart from the above required attributions, neither the name of the copyright
holder nor the names of HOOMD-blue's contributors may be used to endorse or
promote products derived from this software without specific prior written
permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR ANY
WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Maintainer: joaander

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif

#include <boost/python.hpp>
using namespace boost::python;

#include "HarmonicBondForceCompute.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <math.h>

using namespace std;

/*! \file HarmonicBondForceCompute.cc
    \brief Contains code for the HarmonicBondForceCompute class
*/

/*! \param sysdef System to compute forces on
    \param log_suffix Name given to this instance of the harmonic bond
    \post Memory is allocated, and forces are zeroed.
*/
HarmonicBondForceCompute::HarmonicBondForceCompute(boost::shared_ptr<SystemDefinition> sysdef, const std::string& log_suffix) 
    : ForceCompute(sysdef), m_K(NULL), m_r_0(NULL)
    {
    // access the bond data for later use
    m_bond_data = m_sysdef->getBondData();
    m_log_name = std::string( "bond_harmonic_energy") + log_suffix;
    
    // check for some silly errors a user could make
    if (m_bond_data->getNBondTypes() == 0)
        {
        cout << endl << "***Error! No bond types specified" << endl << endl;
        throw runtime_error("Error initializing HarmonicBondForceCompute");
        }
        
    // allocate the parameters
    m_K = new Scalar[m_bond_data->getNBondTypes()];
    m_r_0 = new Scalar[m_bond_data->getNBondTypes()];
    
    }

HarmonicBondForceCompute::~HarmonicBondForceCompute()
    {
    delete[] m_K;
    delete[] m_r_0;
    }

/*! \param type Type of the bond to set parameters for
    \param K Stiffness parameter for the force computation
    \param r_0 Equilibrium length for the force computation

    Sets parameters for the potential of a particular bond type
*/
void HarmonicBondForceCompute::setParams(unsigned int type, Scalar K, Scalar r_0)
    {
    // make sure the type is valid
    if (type >= m_bond_data->getNBondTypes())
        {
        cout << endl << "***Error! Invalid bond type specified" << endl << endl;
        throw runtime_error("Error setting parameters in HarmonicBondForceCompute");
        }
        
    m_K[type] = K;
    m_r_0[type] = r_0;
    
    // check for some silly errors a user could make
    if (K < 0)
        cout << "***Warning! K < 0 specified for harmonic bond" << endl;
    if (r_0 < 0)
        cout << "***Warning! r_0 <= 0 specified for harmonic bond" << endl;
    }

/*! BondForceCompute provides
    - \c harmonic_energy
*/
std::vector< std::string > HarmonicBondForceCompute::getProvidedLogQuantities()
    {
    vector<string> list;
    list.push_back(m_log_name);
    return list;
    }

/*! \param quantity Name of the quantity to get the log value of
    \param timestep Current time step of the simulation
*/
Scalar HarmonicBondForceCompute::getLogValue(const std::string& quantity, unsigned int timestep)
    {
    if (quantity == m_log_name)
        {
        compute(timestep);
        return calcEnergySum();
        }
    else
        {
        cerr << endl << "***Error! " << quantity << " is not a valid log quantity for BondForceCompute" << endl << endl;
        throw runtime_error("Error getting log value");
        }
    }

/*! Actually perform the force computation
    \param timestep Current time step
 */
void HarmonicBondForceCompute::computeForces(unsigned int timestep)
    {
    if (m_prof) m_prof->push("Harmonic");
    
    assert(m_pdata);

    //Accquire necessary arrays
    // access the particle data arrays
    ArrayHandle<Scalar4> h_pos(m_pdata->getPositions(), access_location::host, access_mode::read);
    ArrayHandle<unsigned int> h_rtag(m_pdata->getRTags(), access_location::host, access_mode::read);

    ArrayHandle<Scalar4> h_force(m_force,access_location::host,access_mode::overwrite);
    ArrayHandle<Scalar> h_virial(m_virial,access_location::host,access_mode::overwrite);
    unsigned int virial_pitch = m_virial.getPitch();

    // there are enough other checks on the input data: but it doesn't hurt to be safe
    assert(h_force.data);
    assert(h_virial.data);
    assert(h_pos.data);
    assert(h_rtag.data);
    
    // Zero data for force calculation.
    memset((void*)h_force.data,0,sizeof(Scalar4)*m_force.getNumElements());
    memset((void*)h_virial.data,0,sizeof(Scalar)*m_virial.getNumElements());
    
    // get a local copy of the simulation box too
    const BoxDim& box = m_pdata->getBox();
    // sanity check
    assert(box.xhi > box.xlo && box.yhi > box.ylo && box.zhi > box.zlo);
    
    // precalculate box lenghts
    Scalar Lx = box.xhi - box.xlo;
    Scalar Ly = box.yhi - box.ylo;
    Scalar Lz = box.zhi - box.zlo;
    Scalar Lx2 = Lx / Scalar(2.0);
    Scalar Ly2 = Ly / Scalar(2.0);
    Scalar Lz2 = Lz / Scalar(2.0);
    
   
    // for each of the bonds
    const unsigned int size = (unsigned int)m_bond_data->getNumBonds();
    for (unsigned int i = 0; i < size; i++)
        {
        // lookup the tag of each of the particles participating in the bond
        const Bond& bond = m_bond_data->getBond(i);
        assert(bond.a < m_pdata->getN());
        assert(bond.b < m_pdata->getN());
        
        // transform a and b into indicies into the particle data arrays
        // MEM TRANSFER: 4 ints
        unsigned int idx_a = h_rtag.data[bond.a];
        unsigned int idx_b = h_rtag.data[bond.b];
        assert(idx_a < m_pdata->getN());
        assert(idx_b < m_pdata->getN());
        
        // calculate d\vec{r}
        // MEM_TRANSFER: 6 Scalars / FLOPS 3
        Scalar dx = h_pos.data[idx_b].x - h_pos.data[idx_a].x;
        Scalar dy = h_pos.data[idx_b].y - h_pos.data[idx_a].y;
        Scalar dz = h_pos.data[idx_b].z - h_pos.data[idx_a].z;
        
        // if the vector crosses the box, pull it back
        // (FLOPS: 9 (worst case: first branch is missed, the 2nd is taken and the add is done))
        if (dx >= Lx2)
            dx -= Lx;
        else if (dx < -Lx2)
            dx += Lx;
            
        if (dy >= Ly2)
            dy -= Ly;
        else if (dy < -Ly2)
            dy += Ly;
            
        if (dz >= Lz2)
            dz -= Lz;
        else if (dz < -Lz2)
            dz += Lz;
            
        // sanity check
        assert(dx >= box.xlo && dx < box.xhi);
        assert(dy >= box.ylo && dx < box.yhi);
        assert(dz >= box.zlo && dx < box.zhi);
        
        // on paper, the formula turns out to be: F = K*\vec{r} * (r_0/r - 1)
        // FLOPS: 14 / MEM TRANSFER: 2 Scalars
        Scalar rsq = dx*dx+dy*dy+dz*dz;
        Scalar r = sqrt(rsq);
        Scalar forcemag_divr = m_K[bond.type] * (m_r_0[bond.type] / r - Scalar(1.0));
        if (!isfinite(forcemag_divr))
            forcemag_divr = 0.0f;
        Scalar bond_eng = Scalar(0.5) * Scalar(0.5) * m_K[bond.type] * (m_r_0[bond.type] - r) * (m_r_0[bond.type] - r);
        
        // calculate the virial
        Scalar forcemag_div2r = Scalar(0.5) * forcemag_divr;
        Scalar bond_virialxx = dx * dx * forcemag_div2r;
        Scalar bond_virialxy = dx * dy * forcemag_div2r;
        Scalar bond_virialxz = dx * dz * forcemag_div2r;
        Scalar bond_virialyy = dy * dy * forcemag_div2r;
        Scalar bond_virialyz = dy * dz * forcemag_div2r;
        Scalar bond_virialzz = dz * dz * forcemag_div2r;
        
        // add the force to the particles (FLOPS: 16 / MEM TRANSFER: 20 Scalars)
        h_force.data[idx_b].x += forcemag_divr * dx;
        h_force.data[idx_b].y += forcemag_divr * dy;
        h_force.data[idx_b].z += forcemag_divr * dz;
        h_force.data[idx_b].w += bond_eng;
        h_virial.data[0*virial_pitch+idx_b] += bond_virialxx;
        h_virial.data[1*virial_pitch+idx_b] += bond_virialxy;
        h_virial.data[2*virial_pitch+idx_b] += bond_virialxz;
        h_virial.data[3*virial_pitch+idx_b] += bond_virialyy;
        h_virial.data[4*virial_pitch+idx_b] += bond_virialyz;
        h_virial.data[5*virial_pitch+idx_b] += bond_virialzz;
        
        h_force.data[idx_a].x -= forcemag_divr * dx;
        h_force.data[idx_a].y -= forcemag_divr * dy;
        h_force.data[idx_a].z -= forcemag_divr * dz;
        h_force.data[idx_a].w += bond_eng;
        h_virial.data[0*virial_pitch+idx_a] += bond_virialxx;
        h_virial.data[1*virial_pitch+idx_a] += bond_virialxy;
        h_virial.data[2*virial_pitch+idx_a] += bond_virialxz;
        h_virial.data[3*virial_pitch+idx_a] += bond_virialyy;
        h_virial.data[4*virial_pitch+idx_a] += bond_virialyz;
        h_virial.data[5*virial_pitch+idx_a] += bond_virialzz;
        }
        
    int64_t flops = size*(3 + 9 + 14 + 2 + 16);
    int64_t mem_transfer = m_pdata->getN() * 5 * sizeof(Scalar) + size * ( (4)*sizeof(unsigned int) + (6+2+20)*sizeof(Scalar) );
    if (m_prof) m_prof->pop(flops, mem_transfer);
    }

void export_HarmonicBondForceCompute()
    {
    class_<HarmonicBondForceCompute, boost::shared_ptr<HarmonicBondForceCompute>, bases<ForceCompute>, boost::noncopyable >
    ("HarmonicBondForceCompute", init< boost::shared_ptr<SystemDefinition>, const std::string& >())
    .def("setParams", &HarmonicBondForceCompute::setParams)
    ;
    }

#ifdef WIN32
#pragma warning( pop )
#endif

