/*!
 *
 * Copyright (c) 2021 Kambiz Asadzadeh
 * Copyright (c) 2023 Genyleap
 */

#ifndef INTERFACE_HPP
#define INTERFACE_HPP

#ifdef __has_include
# if __has_include(<common.hpp>)
#   include <common.hpp>
# endif
#else
#   include <common.hpp>
#endif

class Interface
{
public:
    Interface() = default;
    virtual ~Interface() = default;
};

#endif // INTERFACE_HPP
