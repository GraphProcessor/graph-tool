// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2007  Tiago de Paula Peixoto <tiago@forked.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef GRAPH_ABSOLUTE_TRUST_HH
#define GRAPH_ABSOLUTE_TRUST_HH

#include "graph.hh"
#include "graph_filtering.hh"
#include "graph_util.hh"

#include <tr1/unordered_set>
#include <tr1/tuple>
#include <algorithm>

#include "minmax.hh"

#include <iostream>

namespace graph_tool
{
using namespace std;
using namespace boost;
using std::tr1::get;
using std::tr1::tuple;

template <class Path>
struct path_cmp
{
    path_cmp(vector<Path>& paths): _paths(paths) {}
    vector<Path>& _paths;

    typedef size_t first_argument_type;
    typedef size_t second_argument_type;
    typedef bool result_type;
    inline bool operator()(size_t a, size_t b)
    {
        if (get<0>(_paths[a]).second == get<0>(_paths[b]).second)
            return get<1>(_paths[a]).size() > get<1>(_paths[b]).size();
        return get<0>(_paths[a]).second < get<0>(_paths[b]).second;
    }
};

struct get_absolute_trust
{
    template <class Graph, class VertexIndex, class EdgeIndex, class TrustMap,
              class InferredTrustMap>
    void operator()(Graph& g, VertexIndex vertex_index, EdgeIndex edge_index,
                    size_t max_edge_index, int64_t source, TrustMap c,
                    InferredTrustMap t, size_t n_paths, bool reversed) const
    {
        typedef typename graph_traits<Graph>::vertex_descriptor vertex_t;
        typedef typename graph_traits<Graph>::edge_descriptor edge_t;
        typedef typename property_traits<TrustMap>::value_type c_type;
        typedef typename property_traits<InferredTrustMap>::value_type::
            value_type t_type;

        // the path type: the first value is the (trust,weight) pair, the second
        // the set of vertices in the path and the third is the list of edges,
        // in the path sequence.
        typedef tuple<pair<t_type, t_type>, tr1::unordered_set<vertex_t>,
                      vector<edge_t> > path_t;

        int i, N = (source == -1) ? num_vertices(g) : source + 1;
        #pragma omp parallel for default(shared) private(i) schedule(dynamic)
        for (i= (source == -1) ? 0 : source; i < N; ++i)
        {
            vertex_t v = vertex(i, g);
            t[v].resize(num_vertices(g));

            // path priority queue
            vector<path_t> paths(1);
            typedef double_priority_queue<size_t, path_cmp<path_t> > queue_t;
            queue_t queue = queue_t(path_cmp<path_t>(paths));
            get<0>(paths.back()).first = get<0>(paths.back()).second = 1;
            get<1>(paths.back()).insert(v);
            queue.push(0);

            // this is the queue with paths with maximum weights
            queue_t final_queue = queue_t(path_cmp<path_t>(paths));

            // sum of weights that lead to a given vertex
            unchecked_vector_property_map<t_type, VertexIndex>
                weight_sum(vertex_index, num_vertices(g));

            while (!queue.empty())
            {
                size_t pi = queue.top();
                queue.pop_top();
                vertex_t w;

                // push queue top into final queue
                if (get<2>(paths[pi]).size() > 0)
                {
                    w = target(get<2>(paths[pi]).back(), g);
                    final_queue.push(pi);
                }
                else
                {
                    w = v; // the first path
                }

                // if maximum size is reached, remove the bottom
                if ((n_paths > 0) && (final_queue.size() > n_paths))
                {
                    size_t bi = final_queue.bottom();
                    final_queue.pop_bottom();

                    // do not augment if the path is the removed bottom
                    if (bi == pi)
                        continue;
                }

                // augment paths and put them in the queue
                typename graph_traits<Graph>::out_edge_iterator e, e_end;
                for (tie(e, e_end) = out_edges(w, g); e != e_end; ++e)
                {
                    vertex_t a = target(*e, g);
                    // no loops
                    if (get<1>(paths[pi]).find(a) == get<1>(paths[pi]).end())
                    {
                        size_t npi;
                        paths.push_back(paths[pi]); // clone last path
                        npi = paths.size()-1;

                        path_t& np = paths[npi]; // new path

                        if (!reversed)
                        {
                            // path weight
                            get<0>(np).second = get<0>(np).first;

                            // path value
                            get<0>(np).first *= c[*e];
                        }
                        else
                        {
                            if (get<1>(np).size() > 1)
                                get<0>(np).second *= c[*e];
                            get<0>(np).first *= c[*e];
                        }
                        weight_sum[a] += get<0>(np).second;
                        t[v][vertex_index[a]] +=
                            get<0>(np).second*get<0>(np).first;

                        get<1>(np).insert(a);
                        get<2>(np).push_back(*e);

                        // keep following paths only if there is a chance
                        // they will make it into the final queue
                        if ((n_paths > 0 && final_queue.size() < n_paths) ||
                            (final_queue.size() == 0 ||
                             (get<0>(np).second >=
                              get<0>(paths[final_queue.bottom()]).second)))
                            queue.push(npi);
                        else
                            paths.pop_back();
                    }
                }
            }

            int j, N = num_vertices(g);
            #pragma omp parallel for default(shared) private(j) \
                schedule(dynamic)
            for (j = 0; j < N; ++j)
            {
                vertex_t w = vertex(j, g);
                if (w == graph_traits<Graph>::null_vertex())
                    continue;
                if (weight_sum[w] > 0)
                    t[v][w] /= weight_sum[w];
            }
        }
    }

};

}

#endif
