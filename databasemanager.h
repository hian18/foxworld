//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// Datbase Manager
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////

#include "database.h"

class DatabaseManager
{
	public:
		DatabaseManager() {}
		virtual ~DatabaseManager() {}

		static DatabaseManager* getInstance()
		{
			static DatabaseManager instance;
			return &instance;
		}

		bool optimizeTables();

		bool tableExists(std::string table);
		bool triggerExists(std::string trigger);

		int32_t getDatabaseVersion();
		bool isDatabaseSetup();
		uint32_t updateDatabase();
		bool getDatabaseConfig(std::string config, int32_t &value);
		void registerDatabaseConfig(std::string config, int32_t value);
		void checkPasswordType();
		void checkTriggers();
};
