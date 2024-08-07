#include <torch/extension.h>
#include <vector>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <set>
// #include <iostream>

// #include "../eigen/Eigen/Core"
// #include "../eigen/Eigen/SparseCore"
// #include "../eigen/Eigen/SparseLU"
#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <Eigen/SparseLU>

typedef Eigen::SparseMatrix<double> SpMat; // declares a column-major sparse matrix type of double
typedef Eigen::Triplet<double> T;

#define u(i, j, k) u[(i) * n * l + (j) * l + (k)]
#define f(i, j, k) f[(i) * n * l + (j) * l + (k)]
#define get_id(i, j, k) ((i) * n * l + (j) * l + (k))

double calculate_unique_solution(double a1_, double a2_,
                                 double a3_, double f, double h)
{
    double a1 = a1_, a2 = a2_, a3 = a3_, temp;
    if (a1 > a2)
    {
        temp = a1;
        a1 = a2;
        a2 = temp;
    }
    if (a1 > a3)
    {
        temp = a1;
        a1 = a3;
        a3 = temp;
    }
    if (a2 > a3)
    {
        temp = a2;
        a2 = a3;
        a3 = temp;
    }

    double x = a1 + f * h;
    if (x <= a2)
        return x;
    double B = -(a1 + a2);
    double C = (a1 * a1 + a2 * a2 - f * f * h * h) / 2.0;
    x = (-B + sqrt(B * B - 4 * C)) / 2.0;
    if (x <= a3)
        return x;
    B = -2.0 * (a1 + a2 + a3) / 3.0;
    C = (a1 * a1 + a2 * a2 + a3 * a3 - f * f * h * h) / 3.0;
    x = (-B + sqrt(B * B - 4 * C)) / 2.0;
    return x;
}

void sweeping_over_I_J_K(double *u, const double *f, int m, int n, int l, double h, int dirI, int dirJ, int dirK)
{

    auto I = std::make_tuple(dirI == 1 ? 0 : m - 1, dirI == 1 ? m : -1, dirI);
    auto J = std::make_tuple(dirJ == 1 ? 0 : n - 1, dirJ == 1 ? n : -1, dirJ);
    auto K = std::make_tuple(dirK == 1 ? 0 : l - 1, dirK == 1 ? l : -1, dirK);

    for (int i = std::get<0>(I); i != std::get<1>(I); i += std::get<2>(I))
        for (int j = std::get<0>(J); j != std::get<1>(J); j += std::get<2>(J))
            for (int k = std::get<0>(K); k != std::get<1>(K); k += std::get<2>(K))
            {
                double uxmin = i == 0 ? u(i + 1, j, k) : (i == m - 1 ? u(i - 1, j, k) : std::min(u(i + 1, j, k), u(i - 1, j, k)));
                double uymin = j == 0 ? u(i, j + 1, k) : (j == n - 1 ? u(i, j - 1, k) : std::min(u(i, j + 1, k), u(i, j - 1, k)));
                double uzmin = k == 0 ? u(i, j, k + 1) : (k == l - 1 ? u(i, j, k - 1) : std::min(u(i, j, k + 1), u(i, j, k - 1)));
                double u_new = calculate_unique_solution(uxmin, uymin, uzmin, f(i, j, k), h);
                u(i, j, k) = std::min(u_new, u(i, j, k));
            }
}

void sweeping(double *u, const double *f, int m, int n, int l, double h)
{
    sweeping_over_I_J_K(u, f, m, n, l, h, 1, 1, 1);
    sweeping_over_I_J_K(u, f, m, n, l, h, -1, 1, 1);
    sweeping_over_I_J_K(u, f, m, n, l, h, -1, -1, 1);
    sweeping_over_I_J_K(u, f, m, n, l, h, 1, -1, 1);
    sweeping_over_I_J_K(u, f, m, n, l, h, 1, -1, -1);
    sweeping_over_I_J_K(u, f, m, n, l, h, 1, 1, -1);
    sweeping_over_I_J_K(u, f, m, n, l, h, -1, 1, -1);
    sweeping_over_I_J_K(u, f, m, n, l, h, -1, -1, -1);
}

void forward(double *u, const double *u0, const double *f, double h,
             int m, int n, int l, double tol = 1e-8)
{
    memcpy(u, u0, sizeof(double) * m * n * l);
    auto u_old = new double[m * n * l];
    for (int i = 0; i < 20; i++)
    {
        memcpy(u_old, u, sizeof(double) * m * n * l);
        sweeping(u, f, m, n, l, h);
        double err = 0.0;

        for (int j = 0; j < m * n * l; j++)
        {
            err = std::max(fabs(u[j] - u_old[j]), err);
        }

        if (err < tol)
            break;
    }
    delete[] u_old;
}

void backward(
    double *grad_u0, double *grad_f,
    const double *grad_u,
    const double *u, const double *u0, const double *f, double h,
    int m, int n, int l)
{

    Eigen::VectorXd g(m * n * l);
    memcpy(g.data(), grad_u, sizeof(double) * m * n * l);

    // calculate gradients for \partial L/\partial u0
    for (int i = 0; i < m * n * l; i++)
    {
        // if (fabs(u[i] - u0[i])<1e-6) grad_u0[i] = g[i];
        if (u[i] == u0[i])
            grad_u0[i] = g[i];
        else
            grad_u0[i] = 0.0;
    }

    // calculate gradients for \partial L/\partial f
    Eigen::VectorXd rhs(m * n * l);
    for (int i = 0; i < m * n * l; i++)
    {
        rhs[i] = -2 * f[i] * h * h;
    }

    std::vector<T> triplets;
    std::set<int> zero_id;
    for (int i = 0; i < m; i++)
    {
        for (int j = 0; j < n; j++)
        {
            for (int k = 0; k < l; k++)
            {

                int this_id = get_id(i, j, k);

                if (u[this_id] == u0[this_id])
                {
                    // std::cout << "(1) zero id: " << this_id << std::endl;
                    zero_id.insert(this_id);
                    g[this_id] = 0.0;
                    continue;
                }

                double uxmin = i == 0 ? u(i + 1, j, k) : (i == m - 1 ? u(i - 1, j, k) : std::min(u(i + 1, j, k), u(i - 1, j, k)));
                double uymin = j == 0 ? u(i, j + 1, k) : (j == n - 1 ? u(i, j - 1, k) : std::min(u(i, j + 1, k), u(i, j - 1, k)));
                double uzmin = k == 0 ? u(i, j, k + 1) : (k == l - 1 ? u(i, j, k - 1) : std::min(u(i, j, k + 1), u(i, j, k - 1)));

                int idx = i == 0 ? get_id(i + 1, j, k) : (i == m - 1 ? get_id(i - 1, j, k) : (u(i + 1, j, k) > u(i - 1, j, k) ? get_id(i - 1, j, k) : get_id(i + 1, j, k)));
                int idy = j == 0 ? get_id(i, j + 1, k) : (j == n - 1 ? get_id(i, j - 1, k) : (u(i, j + 1, k) > u(i, j - 1, k) ? get_id(i, j - 1, k) : get_id(i, j + 1, k)));
                int idz = k == 0 ? get_id(i, j, k + 1) : (k == l - 1 ? get_id(i, j, k - 1) : (u(i, j, k + 1) > u(i, j, k - 1) ? get_id(i, j, k - 1) : get_id(i, j, k + 1)));

                bool this_id_is_not_zero = false;
                if (u(i, j, k) > uxmin)
                {
                    this_id_is_not_zero = true;
                    triplets.push_back(T(this_id, this_id, 2.0 * (u(i, j, k) - uxmin)));
                    triplets.push_back(T(this_id, idx, -2.0 * (u(i, j, k) - uxmin)));
                }

                if (u(i, j, k) > uymin)
                {
                    this_id_is_not_zero = true;
                    triplets.push_back(T(this_id, this_id, 2.0 * (u(i, j, k) - uymin)));
                    triplets.push_back(T(this_id, idy, -2.0 * (u(i, j, k) - uymin)));
                }

                if (u(i, j, k) > uzmin)
                {
                    this_id_is_not_zero = true;
                    triplets.push_back(T(this_id, this_id, 2.0 * (u(i, j, k) - uzmin)));
                    triplets.push_back(T(this_id, idz, -2.0 * (u(i, j, k) - uzmin)));
                }

                if (!this_id_is_not_zero)
                {
                    // std::cout << "(2) zero id: " << this_id << std::endl;
                    zero_id.insert(this_id);
                    g[this_id] = 0.0;
                //     grad_u0[this_id] = 1.0;
                // }
                // else
                // {
                //     grad_u0[this_id] = 0.0;
                }
            }
        }
    }

    if (zero_id.size() > 0)
    {
        // std::cout << "total zero id: " << zero_id.size() << std::endl;
        for (auto &t : triplets)
        {
            if (zero_id.count(t.col()) || zero_id.count(t.row()))
                t = T(t.col(), t.row(), 0.0);
        }
        for (auto idx : zero_id)
        {
            triplets.push_back(T(idx, idx, 1.0));
        }
    }

    SpMat A(m * n * l, m * n * l);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A = A.transpose();
    Eigen::SparseLU<SpMat> solver;

    solver.analyzePattern(A);
    solver.factorize(A);
    Eigen::VectorXd res = solver.solve(g);
    for (int i = 0; i < m * n * l; i++)
    {
        grad_f[i] = -res[i] * rhs[i];
    }
}

// PyTorch extension interface
torch::Tensor eikonal_forward(torch::Tensor u0, torch::Tensor f, double h)
{
    TORCH_CHECK(u0.dim() == 3, "u0 must be a 3D tensor");
    TORCH_CHECK(f.dim() == 3, "f must be a 3D tensor");
    TORCH_CHECK(u0.sizes() == f.sizes(), "u0 and f must have the same size");
    TORCH_CHECK(u0.is_contiguous() && f.is_contiguous(), "Input tensors must be contiguous");

    int m = u0.size(0);
    int n = u0.size(1);
    int l = u0.size(2);

    auto u = torch::zeros_like(u0);

    forward(u.data_ptr<double>(), u0.data_ptr<double>(), f.data_ptr<double>(), h, m, n, l);

    return u;
}

std::vector<torch::Tensor> eikonal_backward(torch::Tensor grad_u, torch::Tensor u, torch::Tensor u0, torch::Tensor f, double h)
{
    TORCH_CHECK(grad_u.dim() == 3 && u.dim() == 3 && u0.dim() == 3 && f.dim() == 3, "All tensors must be 3D");
    TORCH_CHECK(grad_u.sizes() == u.sizes() && u.sizes() == u0.sizes() && u0.sizes() == f.sizes(), "All tensors must have the same size");
    TORCH_CHECK(grad_u.is_contiguous() && u.is_contiguous() && u0.is_contiguous() && f.is_contiguous(), "All tensors must be contiguous");

    int m = u.size(0);
    int n = u.size(1);
    int l = u.size(2);

    auto grad_u0 = torch::zeros_like(u0);
    auto grad_f = torch::zeros_like(f);

    backward(grad_u0.data_ptr<double>(), grad_f.data_ptr<double>(),
             grad_u.data_ptr<double>(), u.data_ptr<double>(), u0.data_ptr<double>(), f.data_ptr<double>(),
             h, m, n, l);

    return {grad_u0, grad_f};
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.def("forward", &eikonal_forward, "Eikonal3D forward");
    m.def("backward", &eikonal_backward, "Eikonal3D backward");
}