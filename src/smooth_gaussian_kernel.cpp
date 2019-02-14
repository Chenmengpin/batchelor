#include "batchelor.h"
#include "utils.h"

#include <vector>
#include <set>

/* Perform smoothing with the Gaussian kernel */

SEXP smooth_gaussian_kernel(SEXP vect, SEXP index, SEXP data, SEXP sigma) {
    BEGIN_RCPP
    auto correction_vectors=beachmat::create_numeric_matrix(vect);
    const size_t npairs=correction_vectors->get_nrow();
    const size_t ngenes=correction_vectors->get_ncol();
    const Rcpp::IntegerVector _index(index);
    if (npairs!=_index.size()) { 
        throw std::runtime_error("number of rows in 'vect' should be equal to length of 'index'");
    }

    auto mat=beachmat::create_numeric_matrix(data);
    const size_t ncells=mat->get_ncol();
    const size_t ngenes_for_dist=mat->get_nrow();
    const double s2=check_numeric_scalar(sigma, "sigma");
    
    // Constructing the average vector for each MNN cell.
    std::vector<Rcpp::NumericVector> averages(ncells);
    std::set<int> mnncell;
    {
        std::vector<int> number(ncells);
        Rcpp::NumericVector currow(ngenes);
        for (size_t row=0; row<npairs; ++row) {
            correction_vectors->get_row(row, currow.begin());

            int pair_dex=_index[row];
            auto& target=averages[pair_dex];
            ++(number[pair_dex]);
            auto mIt=mnncell.lower_bound(pair_dex);

            if (mIt!=mnncell.end() && *mIt==pair_dex) {
                auto cIt=currow.begin();
                for (auto& t : target){
                    t += *cIt;
                    ++cIt;
                }

            } else {
                target=Rcpp::clone(currow);
                mnncell.insert(mIt, pair_dex);
            }
        }
    
        for (size_t i : mnncell) {
            auto& target=averages[i];
            const auto num=number[i];
            for (auto& t : target) {
                t/=num;
            }
        }
    }

    // Setting up output constructs.
    Rcpp::NumericMatrix output(ngenes, ncells); // yes, this is 'ngenes' not 'ngenes_for_dist'.
    std::vector<double> distances2(ncells), totalprob(ncells);
    Rcpp::NumericVector mnn_incoming(ngenes_for_dist), other_incoming(ngenes_for_dist);

    // Using distances between cells and MNN-involved cells to smooth the correction vector per cell.
    for (size_t mnn : mnncell) {
        auto mnn_iIt=mat->get_const_col(mnn, mnn_incoming.begin());

        for (size_t other=0; other<ncells; ++other) {
            double& curdist2=(distances2[other]=0);
            auto other_iIt=mat->get_const_col(other, other_incoming.begin());
            auto iIt_copy=mnn_iIt;

            for (size_t g=0; g<ngenes_for_dist; ++g) {
                const double tmp=(*iIt_copy  - *other_iIt);
                curdist2+=tmp*tmp;
                ++other_iIt;
                ++iIt_copy;
            }

            // Compute log-probabilities using a Gaussian kernel based on the squared distances.
            // We keep things logged to avoid float underflow, and just ignore the constant bit at the front.
            curdist2/=-s2;
        }
        
        // We sum the probabilities to get the relative MNN density. 
        // This requires some care as the probabilities are still logged at this point.
        double density=0;
        bool starting=true;
        for (const auto& other_mnn : mnncell) {
            if (starting) {
                density=distances2[other_mnn];
                starting=false;
            } else {
                density=R::logspace_add(density, distances2[other_mnn]);
            }
        }

        // Each correction vector is weighted by the Gaussian probability (to account for distance)
        // and density (to avoid being dominated by high-density regions).
        // Summation (and then division, see below) yields smoothed correction vectors.
        const auto& correction = averages[mnn];
        auto oIt=output.begin();
        for (size_t other=0; other<ncells; ++other) {
            const double mult=std::exp(distances2[other] - density);
            totalprob[other]+=mult;

            for (const auto& corval : correction) { 
                (*oIt)+=corval*mult;
                ++oIt;
            }
        }
    }

    // Dividing by the total probability.
    for (size_t other=0; other<ncells; ++other) {
        auto curcol=output.column(other);
        const double total=totalprob[other];

        for (auto& val : curcol) {
            val/=total;
        }
    }

    return output;
    END_RCPP
}