/**
 * @file UID.cpp
 * @brief Implementation of UID and ObjectID base classes.
 */

#include <scene/UID.h>

namespace neurus
{

// --- Static member definition --------------------------------------------

int UID::s_count = 0;

// --- UID -----------------------------------------------------------------

UID::UID()
	: m_id(s_count++)
{
}

// --- ObjectID -------------------------------------------------------------

ObjectID::ObjectID()
	: UID()
{
}

ObjectID::~ObjectID()
{
}

} // namespace neurus
