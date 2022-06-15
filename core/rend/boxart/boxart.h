/*
	Copyright 2022 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include "scraper.h"
#include "../game_scanner.h"
#include "stdclass.h"
#include <unordered_map>
#include <memory>
#include <future>

class Boxart
{
public:
	const GameBoxart *getBoxart(const GameMedia& media);
	std::future<const GameBoxart *> fetchBoxart(const GameMedia& media);
	void saveDatabase();

private:
	void loadDatabase();
	std::string getSaveDirectory() const {
		return get_writable_data_path("/boxart/");
	}

	std::unordered_map<std::string, GameBoxart> games;
	std::unique_ptr<Scraper> scraper;
	bool databaseLoaded = false;
	bool databaseDirty = false;

	static constexpr char const *DB_NAME = "flycast-gamedb.json";
};
