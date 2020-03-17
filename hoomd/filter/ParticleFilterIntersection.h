#ifndef __PARTICLE_FILTER_INTERSECTION_H__
#define __PARTICLE_FILTER_INTERSECTION_H__

#include "ParticleFilter.h"
#include <algorithm>

/// Represents the intersection of two filters: f and g.
class PYBIND11_EXPORT ParticleFilterIntersection : public ParticleFilter
    {
    public:
        /// Constructs the selector
        /// \param f first filter
        /// \param g second filter
        ParticleFilterIntersection(std::shared_ptr<ParticleFilter> f,
                                   std::shared_ptr<ParticleFilter> g)
            : ParticleFilter(), m_f(f), m_g(g) {}

        virtual ~ParticleFilterIntersection() {}

        /// Test if a particle meets the selection criteria
        /// \param sysdef the System Definition
        /// \returns all rank local particles that are in filter m_f and filter
        /// m_g
        virtual std::vector<unsigned int> getSelectedTags(
            std::shared_ptr<SystemDefinition> sysdef) const
            {
            // Get tags for f() and g() as sets
            auto X = m_f->getSelectedTags(sysdef);
            std::sort(X.begin(), X.end());

            auto Y = m_g->getSelectedTags(sysdef);
            std::sort(Y.begin(), Y.end());

            // Create vector and get intersection
            auto tags = std::vector<unsigned int>(std::min(X.size(), Y.size()));
            auto it = set_intersection(X.begin(), X.end(), Y.begin(), Y.end(),
                                       tags.begin());
            tags.resize(it - tags.begin());
            return tags;
            }

    protected:
        std::shared_ptr<ParticleFilter> m_f;
        std::shared_ptr<ParticleFilter> m_g;
    };
#endif
