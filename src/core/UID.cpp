/**
 * @file UID.cpp
 * @brief Implementation of the UID base class.
 */

#include "core/UID.h"

namespace neurus
{

// --- Static member definition --------------------------------------------

int UID::s_count = 0;

// --- UID -----------------------------------------------------------------

UID::UID()
	: o_id(s_count++)
{
}

} // namespace neurus
