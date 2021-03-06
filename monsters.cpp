//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
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
#include "otpch.h"

#include "monsters.h"
#include "monster.h"
#include "container.h"
#include "tools.h"
#include "spells.h"
#include "combat.h"
#include "luascript.h"
#include "weapons.h"
#include "configmanager.h"
#include "game.h"

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

extern Game g_game;
extern Spells* g_spells;
extern Monsters g_monsters;
extern ConfigManager g_config;

MonsterType::MonsterType()
{
	reset();
}

void MonsterType::reset()
{
	experience = 0;

	defense = 0;
	armor = 0;

	canPushItems = false;
	canPushCreatures = false;
	staticAttackChance = 95;
	maxSummons = 0;
	targetDistance = 1;
	runAwayHealth = 0;
	pushable = true;
	base_speed = 200;
	health = 100;
	health_max = 100;

	outfit.lookHead = 0;
	outfit.lookBody = 0;
	outfit.lookLegs = 0;
	outfit.lookFeet = 0;
	outfit.lookType = 0;
	outfit.lookTypeEx = 0;
	outfit.lookAddons = 0;
	lookcorpse = 0;

	conditionImmunities = 0;
	damageImmunities = 0;
	race = RACE_BLOOD;
	isSummonable = false;
	isIllusionable = false;
	isConvinceable = false;
	isAttackable = true;
	isHostile = true;
	isLureable = false;

	lightLevel = 0;
	lightColor = 0;
	skull = SKULL_NONE;
	partyShield = SHIELD_NONE;

	manaCost = 0;
	summonList.clear();
	lootItems.clear();
	elementMap.clear();

	for(SpellList::iterator it = spellAttackList.begin(); it != spellAttackList.end(); ++it)
	{
		if(it->combatSpell)
		{
			delete it->spell;
			it->spell = NULL;
		}
	}

	spellAttackList.clear();
	for(SpellList::iterator it = spellDefenseList.begin(); it != spellDefenseList.end(); ++it)
	{
		if(it->combatSpell)
		{
			delete it->spell;
			it->spell = NULL;
		}
	}

	spellDefenseList.clear();
	yellSpeedTicks = 0;
	yellChance = 0;
	voiceVector.clear();

	changeTargetSpeed = 0;
	changeTargetChance = 0;
	scriptList.clear();
}

MonsterType::~MonsterType()
{
	reset();
}

uint32_t Monsters::getLootRandom()
{
	return random_range(0, MAX_LOOTCHANCE) / g_config.getNumber(ConfigManager::RATE_LOOT);
}

void MonsterType::createLoot(Container* corpse)
{
	ItemVector itemVector;
	for(LootItems::const_iterator it = lootItems.begin(); it != lootItems.end() && (corpse->capacity() - corpse->size() > 0); it++)
	{
		if(Item* tmpItem = createLootItem(*it))
		{
			if(Container* container = tmpItem->getContainer())
			{
				if(createLootContainer(container, (*it), itemVector))
					corpse->__internalAddThing(tmpItem);
				else
					delete container;
			}
			else
			{
				corpse->__internalAddThing(tmpItem);
				itemVector.push_back(tmpItem);
			}
		}
	}

	corpse->__startDecaying();
	uint32_t ownerId = corpse->getCorpseOwner();
	if(ownerId && itemVector.size())
	{
		Player* owner = NULL;
		if((owner = g_game.getPlayerByID(ownerId)) && owner->getParty())
			owner->getParty()->broadcastPartyLoot(name, itemVector);
	}
}

Item* MonsterType::createLootItem(const LootBlock& lootBlock)
{
	Item* tmpItem = NULL;
	if(Item::items[lootBlock.id].stackable)
	{
		uint32_t randvalue = Monsters::getLootRandom();
		if(randvalue < lootBlock.chance)
		{
			uint32_t n = randvalue % lootBlock.countmax + 1;
			tmpItem = Item::CreateItem(lootBlock.id, n);
		}
	}
	else
	{
		if(Monsters::getLootRandom() < lootBlock.chance)
			tmpItem = Item::CreateItem(lootBlock.id, 0);
	}

	if(tmpItem)
	{
		if(lootBlock.subType != -1)
			tmpItem->setSubType(lootBlock.subType);

		if(lootBlock.actionId != -1)
			tmpItem->setActionId(lootBlock.actionId);

		if(lootBlock.uniqueId != -1)
			tmpItem->setUniqueId(lootBlock.uniqueId);

		if(lootBlock.text != "")
			tmpItem->setText(lootBlock.text);

		return tmpItem;
	}

	return NULL;
}

bool MonsterType::createLootContainer(Container* parent, const LootBlock& lootblock, ItemVector& itemVector)
{
	LootItems::const_iterator it = lootblock.childLoot.begin();
	if(it == lootblock.childLoot.end())
		return true;

	for(; it != lootblock.childLoot.end() && parent->size() < parent->capacity(); ++it)
	{
		if(Item* tmpItem = createLootItem((*it)))
		{
			if(Container* container = tmpItem->getContainer())
			{
				if(createLootContainer(container, (*it), itemVector))
					parent->__internalAddThing(container);
				else
					delete container;
			}
			else
			{
				parent->__internalAddThing(tmpItem);
				itemVector.push_back(tmpItem);
			}
		}
	}

	if(parent->size() == 0)
		return false;

	return true;
}

Monsters::Monsters()
{
	loaded = false;
}

bool Monsters::loadFromXml(bool reloading /*= false*/)
{
	loaded = false;
	std::string filename = getFilePath(FILE_TYPE_OTHER, "monster/monsters.xml");

	xmlDocPtr doc = xmlParseFile(filename.c_str());
	if(doc)
	{
		loaded = true;
		xmlNodePtr root, p;
		root = xmlDocGetRootElement(doc);

		if(xmlStrcmp(root->name,(const xmlChar*)"monsters") != 0)
		{
			xmlFreeDoc(doc);
			loaded = false;
			return false;
		}

		p = root->children;
		while(p)
		{
			if(p->type != XML_ELEMENT_NODE)
			{
				p = p->next;
				continue;
			}

			if(xmlStrcmp(p->name, (const xmlChar*)"monster") == 0)
			{
				std::string file;
				std::string name;

				if(readXMLString(p, "file", file) && readXMLString(p, "name", name))
				{
					file = getFilePath(FILE_TYPE_OTHER, "monster/" + file);
					loadMonster(file, name, reloading);
				}
			}
			else
				std::cout << "[Warning - Monsters::loadFromXml]. Unknown node name. " << p->name << std::endl;

			p = p->next;
		}
		xmlFreeDoc(doc);
	}
	return loaded;
}

bool Monsters::reload()
{
	return loadFromXml(true);
}

ConditionDamage* Monsters::getDamageCondition(ConditionType_t conditionType,
	int32_t maxDamage, int32_t minDamage, int32_t startDamage, uint32_t tickInterval)
{
	ConditionDamage* condition = dynamic_cast<ConditionDamage*>(Condition::createCondition(CONDITIONID_COMBAT, conditionType, 0, 0));
	condition->setParam(CONDITIONPARAM_TICKINTERVAL, tickInterval);
	condition->setParam(CONDITIONPARAM_MINVALUE, minDamage);
	condition->setParam(CONDITIONPARAM_MAXVALUE, maxDamage);
	condition->setParam(CONDITIONPARAM_STARTVALUE, startDamage);
	condition->setParam(CONDITIONPARAM_DELAYED, 1);
	return condition;
}

bool Monsters::deserializeSpell(xmlNodePtr node, spellBlock_t& sb, const std::string& description)
{
	sb.chance = 100;
	sb.speed = 2000;
	sb.range = 0;
	sb.minCombatValue = 0;
	sb.maxCombatValue = 0;
	sb.combatSpell = false;
	sb.isMelee = false;

	std::string name = "", scriptName = "";
	bool isScripted = false;

	if(readXMLString(node, "script", scriptName))
		isScripted = true;
	else if(!readXMLString(node, "name", name))
		return false;

	int32_t intValue;
	std::string strValue;
	if(readXMLInteger(node, "speed", intValue) || readXMLInteger(node, "interval", intValue))
		sb.speed = std::max(1, intValue);

	if(readXMLInteger(node, "chance", intValue))
	{
		if(intValue < 0 || intValue > 100)
			intValue = 100;

		sb.chance = intValue;
	}

	if(readXMLInteger(node, "range", intValue))
	{
		if(intValue < 0 )
			intValue = 0;

		if(intValue > Map::maxViewportX * 2)
			intValue = Map::maxViewportX * 2;

		sb.range = intValue;
	}

	if(readXMLInteger(node, "min", intValue))
		sb.minCombatValue = intValue;

	if(readXMLInteger(node, "max", intValue))
	{
		sb.maxCombatValue = intValue;

		//normalize values
		if(std::abs(sb.minCombatValue) > std::abs(sb.maxCombatValue))
		{
			int32_t value = sb.maxCombatValue;
			sb.maxCombatValue = sb.minCombatValue;
			sb.minCombatValue = value;
		}
	}

	if((sb.spell = g_spells->getSpellByName(name)))
		return true;

	CombatSpell* combatSpell = NULL;
	bool needTarget = false, needDirection = false;
	if(isScripted)
	{
		if(readXMLInteger(node, "direction", intValue))
			needDirection = (intValue == 1);

		if(readXMLInteger(node, "target", intValue))
			needTarget = (intValue != 0);

		combatSpell = new CombatSpell(NULL, needTarget, needDirection);
		if(!combatSpell->loadScript(getFilePath(FILE_TYPE_OTHER, g_spells->getScriptBaseName() + "/scripts/" + scriptName)))
		{
			delete combatSpell;
			return false;
		}

		if(!combatSpell->loadScriptCombat())
		{
			delete combatSpell;
			return false;

		}

		combatSpell->getCombat()->setPlayerCombatValues(FORMULA_VALUE, sb.minCombatValue, 0, sb.maxCombatValue, 0);
	}
	else
	{
		Combat* combat = new Combat;
		sb.combatSpell = true;
		if(readXMLInteger(node, "length", intValue))
		{
			int32_t length = intValue;
			if(length > 0)
			{
				int32_t spread = 3;

				//need direction spell
				if(readXMLInteger(node, "spread", intValue))
					spread = std::max(0, intValue);

				AreaCombat* area = new AreaCombat();
				area->setupArea(length, spread);
				combat->setArea(area);

				needDirection = true;
			}
		}

		if(readXMLInteger(node, "radius", intValue))
		{
			int32_t radius = intValue;

			//target spell
			if(readXMLInteger(node, "target", intValue))
				needTarget = (intValue != 0);

			AreaCombat* area = new AreaCombat();
			area->setupArea(radius);
			combat->setArea(area);
		}

		std::string tmpName = asLowerCaseString(name);
		if(tmpName == "melee")
		{
			int32_t attack = 0, skill = 0;
			sb.isMelee = true;
			if(readXMLInteger(node, "attack", attack) && readXMLInteger(node, "skill", skill))
			{
					sb.minCombatValue = 0;
					sb.maxCombatValue = -Weapons::getMaxMeleeDamage(skill, attack);
			}

			ConditionType_t conditionType = CONDITION_NONE;
			int32_t startDamage = 0, minDamage = 0, maxDamage = 0;
			uint32_t tickInterval = 2000;
			if(readXMLInteger(node, "fire", intValue))
			{
				conditionType = CONDITION_FIRE;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}
			else if(readXMLInteger(node, "energy", intValue))
			{
				conditionType = CONDITION_ENERGY;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}
			else if(readXMLInteger(node, "poison", intValue) || readXMLInteger(node, "earth", intValue))
			{
				conditionType = CONDITION_POISON;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 5000;
			}
			else if(readXMLInteger(node, "freeze", intValue))
			{
				conditionType = CONDITION_FREEZING;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}
			else if(readXMLInteger(node, "dazzle", intValue))
			{
				conditionType = CONDITION_DAZZLED;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}
			else if(readXMLInteger(node, "curse", intValue))
			{
				conditionType = CONDITION_CURSED;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}
			else if(readXMLInteger(node, "drown", intValue))
			{
				conditionType = CONDITION_DROWN;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}
			else if(readXMLInteger(node, "physical", intValue))
			{
				conditionType = CONDITION_PHYSICAL;

				minDamage = intValue;
				maxDamage = intValue;
				tickInterval = 10000;
			}

			if(readXMLInteger(node, "tick", intValue) && intValue > 0)
				tickInterval = intValue;

			if(conditionType != CONDITION_NONE)
			{
				Condition* condition = getDamageCondition(conditionType, maxDamage, minDamage, startDamage, tickInterval);
				if(condition)
					combat->setCondition(condition);
			}

			sb.range = 1;
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_PHYSICALDAMAGE);
			combat->setParam(COMBATPARAM_BLOCKEDBYARMOR, 1);
			combat->setParam(COMBATPARAM_BLOCKEDBYSHIELD, 1);
		}
		else if(tmpName == "physical")
		{
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_PHYSICALDAMAGE);
			combat->setParam(COMBATPARAM_BLOCKEDBYARMOR, 1);
		}
		else if(tmpName == "drown")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_DROWNDAMAGE);
		else if(tmpName == "fire")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_FIREDAMAGE);
		else if(tmpName == "energy")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_ENERGYDAMAGE);
		else if(tmpName == "poison" || tmpName == "earth")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_EARTHDAMAGE);
		else if(tmpName == "ice")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_ICEDAMAGE);
		else if(tmpName == "holy")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_HOLYDAMAGE);
		else if(tmpName == "death")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_DEATHDAMAGE);
		else if(tmpName == "lifedrain")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_LIFEDRAIN);
		else if(tmpName == "manadrain")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_MANADRAIN);
		else if(tmpName == "healing")
		{
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_HEALING);
			combat->setParam(COMBATPARAM_AGGRESSIVE, 0);
		}
		else if(tmpName == "undefined")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_UNDEFINEDDAMAGE);
		else if(tmpName == "speed")
		{
			int32_t speedChange = 0, duration = 10000;
			if(readXMLInteger(node, "duration", intValue))
				duration = intValue;

			if(readXMLInteger(node, "speedchange", intValue))
			{
				speedChange = intValue;
				if(speedChange < -1000)
				{
					//cant be slower than 100%
					speedChange = -1000;
				}
			}

			ConditionType_t conditionType;
			if(speedChange > 0)
			{
				conditionType = CONDITION_HASTE;
				combat->setParam(COMBATPARAM_AGGRESSIVE, 0);
			}
			else
				conditionType = CONDITION_PARALYZE;

			ConditionSpeed* condition = dynamic_cast<ConditionSpeed*>(Condition::createCondition(CONDITIONID_COMBAT, conditionType, duration, 0));
			if(condition)
			{
				condition->setFormulaVars(speedChange / 1000.0, 0, speedChange / 1000.0, 0);
				combat->setCondition(condition);
			}
		}
		else if(tmpName == "outfit")
		{
			int32_t duration = 10000;
			if(readXMLInteger(node, "duration", intValue))
				duration = intValue;

			if(readXMLString(node, "monster", strValue))
			{
				MonsterType* mType = g_monsters.getMonsterType(strValue);
				if(mType)
				{
					ConditionOutfit* condition = dynamic_cast<ConditionOutfit*>(Condition::createCondition(CONDITIONID_COMBAT, CONDITION_OUTFIT, duration, 0));
					if(condition)
					{
						condition->addOutfit(mType->outfit);
						combat->setParam(COMBATPARAM_AGGRESSIVE, 0);
						combat->setCondition(condition);
					}
				}
			}
			else if(readXMLInteger(node, "item", intValue))
			{
				Outfit_t outfit;
				outfit.lookTypeEx = intValue;

				ConditionOutfit* condition = dynamic_cast<ConditionOutfit*>(Condition::createCondition(CONDITIONID_COMBAT, CONDITION_OUTFIT, duration, 0));
				if(condition)
				{
					condition->addOutfit(outfit);
					combat->setParam(COMBATPARAM_AGGRESSIVE, 0);
					combat->setCondition(condition);
				}
			}
		}
		else if(tmpName == "invisible")
		{
			int32_t duration = 10000;
			if(readXMLInteger(node, "duration", intValue))
				duration = intValue;

			Condition* condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_INVISIBLE, duration, 0);
			if(condition)
			{
				combat->setParam(COMBATPARAM_AGGRESSIVE, 0);
				combat->setCondition(condition);
			}
		}
		else if(tmpName == "drunk")
		{
			int32_t duration = 10000;
			if(readXMLInteger(node, "duration", intValue))
				duration = intValue;

			Condition* condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_DRUNK, duration, 0);
			if(condition)
				combat->setCondition(condition);
		}
		else if(tmpName == "firefield")
			combat->setParam(COMBATPARAM_CREATEITEM, 1492);
		else if(tmpName == "poisonfield")
			combat->setParam(COMBATPARAM_CREATEITEM, 1496);
		else if(tmpName == "energyfield")
			combat->setParam(COMBATPARAM_CREATEITEM, 1495);
		else if(tmpName == "firecondition" || tmpName == "energycondition" || tmpName == "drowncondition" ||
			tmpName == "poisoncondition" || tmpName == "earthcondition" || tmpName == "freezecondition" ||
			tmpName == "cursecondition" || tmpName == "dazzlecondition")
		{
			ConditionType_t conditionType = CONDITION_NONE;
			uint32_t tickInterval = 2000;
			if(tmpName == "firecondition")
			{
				conditionType = CONDITION_FIRE;
				tickInterval = 10000;
			}
			else if(tmpName == "energycondition")
			{
				conditionType = CONDITION_ENERGY;
				tickInterval = 10000;
			}
			else if(tmpName == "poisoncondition" || tmpName == "earthcondition")
			{
				conditionType = CONDITION_POISON;
				tickInterval = 5000;
			}
			else if(tmpName == "freezecondition")
			{
				conditionType = CONDITION_FREEZING;
				tickInterval = 10000;
			}
			else if(tmpName == "cursecondition")
			{
				conditionType = CONDITION_CURSED;
				tickInterval = 10000;
			}
			else if(tmpName == "dazzlecondition")
			{
				conditionType = CONDITION_CURSED;
				tickInterval = 10000;
			}
			else if(tmpName == "drowncondition")
			{
				conditionType = CONDITION_DROWN;
				tickInterval = 5000;
			}
			else if(tmpName == "physicalcondition")
			{
				conditionType = CONDITION_PHYSICAL;
				tickInterval = 5000;
			}

			if(readXMLInteger(node, "tick", intValue) && intValue > 0)
				tickInterval = intValue;

			int32_t startDamage = 0, minDamage = std::abs(sb.minCombatValue), maxDamage = std::abs(sb.maxCombatValue);
			if(readXMLInteger(node, "start", intValue))
			{
				intValue = std::abs(intValue);
				if(intValue <= minDamage)
					startDamage = intValue;
			}

			Condition* condition = getDamageCondition(conditionType, maxDamage, minDamage, startDamage, tickInterval);
			if(condition)
				combat->setCondition(condition);
		}
		else if(tmpName == "strength")
		{
			//TODO?
		}
		else
		{
			std::cout << "Error: [Monsters::deserializeSpell] - " << description <<  " - Unknown spell name: " << name << std::endl;
			delete combat;
			return false;
		}

		combat->setPlayerCombatValues(FORMULA_VALUE, sb.minCombatValue, 0, sb.maxCombatValue, 0);
		combatSpell = new CombatSpell(combat, needTarget, needDirection);

		xmlNodePtr attributeNode = node->children;
		while(attributeNode)
		{
			if(xmlStrcmp(attributeNode->name, (const xmlChar*)"attribute") == 0)
			{
				if(readXMLString(attributeNode, "key", strValue))
				{
					std::string tmpStrValue = asLowerCaseString(strValue);
					if(tmpStrValue == "shooteffect")
					{
						if(readXMLString(attributeNode, "value", strValue))
						{
							ShootType_t shoot = getShootType(strValue);
							if(shoot != NM_SHOOT_UNK)
								combat->setParam(COMBATPARAM_DISTANCEEFFECT, shoot);
							else
								std::cout << "Warning: [Monsters::deserializeSpell] - "  << description << " - Unknown shootEffect: " << strValue << std::endl;
						}
					}
					else if(tmpStrValue == "areaeffect")
					{
						if(readXMLString(attributeNode, "value", strValue))
						{
							MagicEffectClasses effect = getMagicEffect(strValue);
							if(effect != NM_ME_UNK)
								combat->setParam(COMBATPARAM_EFFECT, effect);
							else
								std::cout << "Warning: [Monsters::deserializeSpell] - "  << description << " - Unknown areaEffect: " << strValue << std::endl;
						}
					}
					else
						std::cout << "[Warning - Monsters::deserializeSpells] Effect type \"" << strValue << "\" does not exist." << std::endl;
				}
			}
			attributeNode = attributeNode->next;
		}
	}

	sb.spell = combatSpell;
	return true;
}

#define SHOW_XML_WARNING(desc) std::cout << "[Warning - Monsters::loadMonster] " << desc << ". " << file << std::endl;
#define SHOW_XML_ERROR(desc) std::cout << "[Error - Monsters::loadMonster] " << desc << ". " << file << std::endl;

bool Monsters::loadMonster(const std::string& file, const std::string& monster_name, bool reloading /*= false*/)
{
	bool monsterLoad, new_mType = true;
	MonsterType* mType = NULL;
	if(reloading)
	{
		uint32_t id = getIdByName(monster_name);
		if(id != 0)
		{
			mType = getMonsterType(id);
			if(mType != NULL)
			{
				new_mType = false;
				mType->reset();
			}
		}
	}

	if(new_mType)
		mType = new MonsterType();

	monsterLoad = true;
	xmlDocPtr doc = xmlParseFile(file.c_str());
	if(doc)
	{
		xmlNodePtr root, p;
		root = xmlDocGetRootElement(doc);

		if(xmlStrcmp(root->name,(const xmlChar*)"monster") != 0)
			std::cerr << "Malformed XML: " << file << std::endl;

		int32_t intValue;
		std::string strValue;

		p = root->children;

		if(readXMLString(root, "name", strValue))
			mType->name = strValue;
		else
			monsterLoad = false;

		if(readXMLString(root, "nameDescription", strValue))
			mType->nameDescription = strValue;
		else
		{
			mType->nameDescription = "a " + mType->name;
			toLowerCaseString(mType->nameDescription);
		}

		if(readXMLString(root, "race", strValue))
		{
			std::string tmpStrValue = asLowerCaseString(strValue);
			if(tmpStrValue == "venom" || atoi(strValue.c_str()) == 1)
				mType->race = RACE_VENOM;
			else if(tmpStrValue == "blood" || atoi(strValue.c_str()) == 2)
				mType->race = RACE_BLOOD;
			else if(tmpStrValue == "undead" || atoi(strValue.c_str()) == 3)
				mType->race = RACE_UNDEAD;
			else if(tmpStrValue == "fire" || atoi(strValue.c_str()) == 4)
				mType->race = RACE_FIRE;
			else if(tmpStrValue == "energy" || atoi(strValue.c_str()) == 5)
				mType->race = RACE_ENERGY;
			else
				SHOW_XML_WARNING("Unknown race type " << strValue);
		}

		if(readXMLInteger(root, "experience", intValue))
			mType->experience = intValue;

		if(readXMLInteger(root, "speed", intValue))
			mType->base_speed = intValue;

		if(readXMLInteger(root, "manacost", intValue))
			mType->manaCost = intValue;

		while(p)
		{
			if(p->type != XML_ELEMENT_NODE)
			{
				p = p->next;
				continue;
			}

			if(xmlStrcmp(p->name, (const xmlChar*)"health") == 0)
			{
				if(readXMLInteger(p, "now", intValue))
					mType->health = intValue;
				else
				{
					SHOW_XML_ERROR("Missing health.now");
					monsterLoad = false;
				}

				if(readXMLInteger(p, "max", intValue))
					mType->health_max = intValue;
				else
				{
					SHOW_XML_ERROR("Missing health.max");
					monsterLoad = false;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"flags") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"flag") == 0)
					{
						if(readXMLInteger(tmpNode, "summonable", intValue))
							mType->isSummonable = (intValue != 0);

						if(readXMLInteger(tmpNode, "attackable", intValue))
							mType->isAttackable = (intValue != 0);

						if(readXMLInteger(tmpNode, "hostile", intValue))
							mType->isHostile = (intValue != 0);

						if(readXMLInteger(tmpNode, "illusionable", intValue))
							mType->isIllusionable = (intValue != 0);

						if(readXMLInteger(tmpNode, "convinceable", intValue))
							mType->isConvinceable = (intValue != 0);

						if(readXMLInteger(tmpNode, "pushable", intValue))
							mType->pushable = (intValue != 0);

						if(readXMLInteger(tmpNode, "canpushitems", intValue))
							mType->canPushItems = (intValue != 0);

						if(readXMLInteger(tmpNode, "canpushcreatures", intValue))
							mType->canPushCreatures = (intValue != 0);

						if(readXMLInteger(tmpNode, "staticattack", intValue))
						{
							if(intValue < 0)
							{
								SHOW_XML_WARNING("staticattack lower than 0");
								intValue = 0;
							}

							if(intValue > 100)
							{
								SHOW_XML_WARNING("staticattack greater than 100");
								intValue = 100;
							}

							mType->staticAttackChance = intValue;
						}

						if(readXMLInteger(tmpNode, "lightlevel", intValue))
							mType->lightLevel = intValue;

						if(readXMLInteger(tmpNode, "lightcolor", intValue))
							mType->lightColor = intValue;

						if(readXMLInteger(tmpNode, "targetdistance", intValue))
						{
							if(intValue > Map::maxViewportX)
								SHOW_XML_WARNING("targetdistance greater than maxViewportX");

							mType->targetDistance = std::max(1, intValue);
						}

						if(readXMLInteger(tmpNode, "runonhealth", intValue))
							mType->runAwayHealth = intValue;

						if(readXMLInteger(tmpNode, "lureable", intValue))
							mType->isLureable = (intValue != 0);

						if(readXMLString(tmpNode, "skull", strValue))
						{
							std::string tmpStrValue = asLowerCaseString(strValue);
							if(tmpStrValue == "red" || tmpStrValue == "4")
								mType->skull = SKULL_RED;
							else if(tmpStrValue == "white" || tmpStrValue == "3")
								mType->skull = SKULL_WHITE;
							else if(tmpStrValue == "green" || tmpStrValue == "2")
								mType->skull = SKULL_GREEN;
							else if(tmpStrValue == "yellow" || tmpStrValue == "1")
								mType->skull = SKULL_YELLOW;
							else
								mType->skull = SKULL_NONE;
						}

						if(readXMLString(tmpNode, "shield", strValue))
						{
							std::string tmpStrValue = asLowerCaseString(strValue);
							if(tmpStrValue == "whitenoshareoff" || tmpStrValue == "10")
								mType->partyShield = SHIELD_YELLOW_NOSHAREDEXP;
							else if(tmpStrValue == "blueshareoff" || tmpStrValue == "9")
								mType->partyShield = SHIELD_BLUE_NOSHAREDEXP;
							else if(tmpStrValue == "yellowshareblink" || tmpStrValue == "8")
								mType->partyShield = SHIELD_YELLOW_NOSHAREDEXP_BLINK;
							else if(tmpStrValue == "blueshareblink" || tmpStrValue == "7")
								mType->partyShield = SHIELD_BLUE_NOSHAREDEXP_BLINK;
							else if(tmpStrValue == "yellowshareon" || tmpStrValue == "6")
								mType->partyShield = SHIELD_YELLOW_SHAREDEXP;
							else if(tmpStrValue == "blueshareon" || tmpStrValue == "5")
								mType->partyShield = SHIELD_BLUE_SHAREDEXP;
							else if(tmpStrValue == "yellow" || tmpStrValue == "4")
								mType->partyShield = SHIELD_YELLOW;
							else if(tmpStrValue == "blue" || tmpStrValue == "3")
								mType->partyShield = SHIELD_BLUE;
							else if(tmpStrValue == "whiteyellow" || tmpStrValue == "2")
								mType->partyShield = SHIELD_WHITEYELLOW;
							else if(tmpStrValue == "whiteblue" || tmpStrValue == "1")
								mType->partyShield = SHIELD_WHITEBLUE;
							else
								mType->partyShield = SHIELD_NONE;
						}
					}
					tmpNode = tmpNode->next;
				}

				//if a monster can push creatures, it should not be pushable
				if(mType->canPushCreatures && mType->pushable)
					mType->pushable = false;
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"targetchange") == 0)
			{
				if(readXMLInteger(p, "speed", intValue) || readXMLInteger(p, "interval", intValue))
					mType->changeTargetSpeed = std::max(1, intValue);
				else
					SHOW_XML_WARNING("Missing targetchange.speed");

				if(readXMLInteger(p, "chance", intValue))
					mType->changeTargetChance = intValue;
				else
					SHOW_XML_WARNING("Missing targetchange.chance");
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"strategy") == 0)
			{
				if(readXMLInteger(p, "attack", intValue)){}
					//mType->attackStrength = intValue;

				if(readXMLInteger(p, "defense", intValue)){}
					//mType->defenseStrength = intValue;
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"look") == 0)
			{
				if(readXMLInteger(p, "type", intValue))
				{
					mType->outfit.lookType = intValue;
					if(readXMLInteger(p, "head", intValue))
						mType->outfit.lookHead = intValue;

					if(readXMLInteger(p, "body", intValue))
						mType->outfit.lookBody = intValue;

					if(readXMLInteger(p, "legs", intValue))
						mType->outfit.lookLegs = intValue;

					if(readXMLInteger(p, "feet", intValue))
						mType->outfit.lookFeet = intValue;

					if(readXMLInteger(p, "addons", intValue))
						mType->outfit.lookAddons = intValue;
				}
				else if(readXMLInteger(p, "typeex", intValue))
					mType->outfit.lookTypeEx = intValue;
				else
					SHOW_XML_WARNING("Missing look type/typeex");

				if(readXMLInteger(p, "corpse", intValue))
					mType->lookcorpse = intValue;
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"attacks") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"attack") == 0)
					{
						spellBlock_t sb;
						if(deserializeSpell(tmpNode, sb, monster_name))
							mType->spellAttackList.push_back(sb);
						else
							SHOW_XML_WARNING("Cant load spell");
					}
					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"defenses") == 0)
			{
				if(readXMLInteger(p, "defense", intValue))
					mType->defense = intValue;

				if(readXMLInteger(p, "armor", intValue))
					mType->armor = intValue;

				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"defense") == 0)
					{
						spellBlock_t sb;
						if(deserializeSpell(tmpNode, sb, monster_name))
							mType->spellDefenseList.push_back(sb);
						else
							SHOW_XML_WARNING("Cant load spell");
					}

					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"immunities") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"immunity") == 0)
					{
						if(readXMLString(tmpNode, "name", strValue))
						{
							std::string tmpStrValue = asLowerCaseString(strValue);
							if(tmpStrValue == "physical")
							{
								mType->damageImmunities |= COMBAT_PHYSICALDAMAGE;
								mType->conditionImmunities |= CONDITION_PHYSICAL;
							}
							else if(tmpStrValue == "energy")
							{
								mType->damageImmunities |= COMBAT_ENERGYDAMAGE;
								mType->conditionImmunities |= CONDITION_ENERGY;
							}
							else if(tmpStrValue == "fire")
							{
								mType->damageImmunities |= COMBAT_FIREDAMAGE;
								mType->conditionImmunities |= CONDITION_FIRE;
							}
							else if(tmpStrValue == "poison" ||
								tmpStrValue == "earth")
							{
								mType->damageImmunities |= COMBAT_EARTHDAMAGE;
								mType->conditionImmunities |= CONDITION_POISON;
							}
							else if(tmpStrValue == "ice")
							{
								mType->damageImmunities |= COMBAT_ICEDAMAGE;
								mType->conditionImmunities |= CONDITION_FREEZING;
							}
							else if(tmpStrValue == "holy")
							{
								mType->damageImmunities |= COMBAT_HOLYDAMAGE;
								mType->conditionImmunities |= CONDITION_DAZZLED;
							}
							else if(tmpStrValue == "death")
							{
								mType->damageImmunities |= COMBAT_DEATHDAMAGE;
								mType->conditionImmunities |= CONDITION_CURSED;
							}
							else if(tmpStrValue == "drown")
							{
								mType->damageImmunities |= COMBAT_DROWNDAMAGE;
								mType->conditionImmunities |= CONDITION_DROWN;
							}
							else if(tmpStrValue == "lifedrain")
								mType->damageImmunities |= COMBAT_LIFEDRAIN;
							else if(tmpStrValue == "manadrain")
								mType->damageImmunities |= COMBAT_MANADRAIN;
							else if(tmpStrValue == "paralyze")
								mType->conditionImmunities |= CONDITION_PARALYZE;
							else if(tmpStrValue == "outfit")
								mType->conditionImmunities |= CONDITION_OUTFIT;
							else if(tmpStrValue == "drunk")
								mType->conditionImmunities |= CONDITION_DRUNK;
							else if(tmpStrValue == "invisible")
								mType->conditionImmunities |= CONDITION_INVISIBLE;
							else
								SHOW_XML_WARNING("Unknown immunity name " << strValue);
						}
						//old immunities code
						else if(readXMLInteger(tmpNode, "physical", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_PHYSICALDAMAGE;
								//mType->conditionImmunities |= CONDITION_PHYSICAL;
							}
						}
						else if(readXMLInteger(tmpNode, "energy", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_ENERGYDAMAGE;
								mType->conditionImmunities |= CONDITION_ENERGY;
							}
						}
						else if(readXMLInteger(tmpNode, "fire", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_FIREDAMAGE;
								mType->conditionImmunities |= CONDITION_FIRE;
							}
						}
						else if(readXMLInteger(tmpNode, "poison", intValue) ||
							readXMLInteger(tmpNode, "earth", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_EARTHDAMAGE;
								mType->conditionImmunities |= CONDITION_POISON;
							}
						}
						else if(readXMLInteger(tmpNode, "drown", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_DROWNDAMAGE;
								mType->conditionImmunities |= CONDITION_DROWN;
							}
						}
						else if(readXMLInteger(tmpNode, "ice", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_ICEDAMAGE;
								mType->conditionImmunities |= CONDITION_FREEZING;
							}
						}
						else if(readXMLInteger(tmpNode, "holy", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_HOLYDAMAGE;
								mType->conditionImmunities |= CONDITION_DAZZLED;
							}
						}
						else if(readXMLInteger(tmpNode, "death", intValue))
						{
							if(intValue != 0)
							{
								mType->damageImmunities |= COMBAT_DEATHDAMAGE;
								mType->conditionImmunities |= CONDITION_CURSED;
							}
						}
						else if(readXMLInteger(tmpNode, "lifedrain", intValue))
						{
							if(intValue != 0)
								mType->damageImmunities |= COMBAT_LIFEDRAIN;
						}
						else if(readXMLInteger(tmpNode, "manadrain", intValue))
						{
							if(intValue != 0)
								mType->damageImmunities |= COMBAT_LIFEDRAIN;
						}
						else if(readXMLInteger(tmpNode, "paralyze", intValue))
						{
							if(intValue != 0)
								mType->conditionImmunities |= CONDITION_PARALYZE;
						}
						else if(readXMLInteger(tmpNode, "outfit", intValue))
						{
							if(intValue != 0)
								mType->conditionImmunities |= CONDITION_OUTFIT;
						}
						else if(readXMLInteger(tmpNode, "drunk", intValue))
						{
							if(intValue != 0)
								mType->conditionImmunities |= CONDITION_DRUNK;
						}
						else if(readXMLInteger(tmpNode, "invisible", intValue))
						{
							if(intValue != 0)
								mType->conditionImmunities |= CONDITION_INVISIBLE;
						}
					}

					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"voices") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				if(readXMLInteger(p, "speed", intValue) || readXMLInteger(p, "interval", intValue))
					mType->yellSpeedTicks = intValue;
				else
					SHOW_XML_WARNING("Missing voices.speed");

				if(readXMLInteger(p, "chance", intValue))
					mType->yellChance = intValue;
				else
					SHOW_XML_WARNING("Missing voices.chance");

				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"voice") == 0)
					{
						voiceBlock_t vb;
						vb.text = "";
						vb.yellText = false;

						if(readXMLString(tmpNode, "sentence", strValue))
							vb.text = strValue;
						else
							SHOW_XML_WARNING("Missing voice.sentence");

						if(readXMLInteger(tmpNode, "yell", intValue))
							vb.yellText = (intValue != 0);

						mType->voiceVector.push_back(vb);
					}

					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"loot") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(tmpNode->type != XML_ELEMENT_NODE)
					{
						tmpNode = tmpNode->next;
						continue;
					}

					LootBlock lootBlock;
					if(loadLootItem(tmpNode, lootBlock))
						mType->lootItems.push_back(lootBlock);
					else
						SHOW_XML_WARNING("Cant load loot");

					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"elements") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"element") == 0)
					{
						if(readXMLInteger(tmpNode, "firePercent", intValue))
							mType->elementMap[COMBAT_FIREDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "energyPercent", intValue))
							mType->elementMap[COMBAT_ENERGYDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "icePercent", intValue))
							mType->elementMap[COMBAT_ICEDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "poisonPercent", intValue) ||
							readXMLInteger(tmpNode, "earthPercent", intValue))
							mType->elementMap[COMBAT_EARTHDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "holyPercent", intValue))
							mType->elementMap[COMBAT_HOLYDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "deathPercent", intValue))
							mType->elementMap[COMBAT_DEATHDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "drownPercent", intValue))
							mType->elementMap[COMBAT_DROWNDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "physicalPercent", intValue))
							mType->elementMap[COMBAT_PHYSICALDAMAGE] = intValue;
						else if(readXMLInteger(tmpNode, "lifeDrainPercent", intValue))
							mType->elementMap[COMBAT_LIFEDRAIN] = intValue;
						else if(readXMLInteger(tmpNode, "manaDrainPercent", intValue))
							mType->elementMap[COMBAT_MANADRAIN] = intValue;
						else if(readXMLInteger(tmpNode, "healingPercent", intValue))
							mType->elementMap[COMBAT_HEALING] = intValue;
						else if(readXMLInteger(tmpNode, "undefinedPercent", intValue))
							mType->elementMap[COMBAT_UNDEFINEDDAMAGE] = intValue;
					}

					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"summons") == 0)
			{
				if(readXMLInteger(p, "maxSummons", intValue))
					mType->maxSummons = std::min(intValue, 100);
				else
					SHOW_XML_WARNING("Missing summons.maxSummons");

				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"summon") == 0)
					{
						uint32_t chance = 100;
						uint32_t interval = 1000;
						uint32_t amount = 1;

						if(readXMLInteger(tmpNode, "speed", intValue) || readXMLInteger(tmpNode, "interval", intValue))
							interval = intValue;

						if(readXMLInteger(tmpNode, "chance", intValue))
							chance = intValue;

						if(readXMLInteger(tmpNode, "amount", intValue) || readXMLInteger(tmpNode, "max", intValue))
							amount = intValue;

						if(readXMLString(tmpNode, "name", strValue))
						{
							summonBlock_t sb;
							sb.name = strValue;
							sb.interval = interval;
							sb.chance = chance;
							sb.amount = amount;

							mType->summonList.push_back(sb);
						}
						else
							SHOW_XML_WARNING("Missing summon.name");
					}
					tmpNode = tmpNode->next;
				}
			}
			else if(xmlStrcmp(p->name, (const xmlChar*)"script") == 0)
			{
				xmlNodePtr tmpNode = p->children;
				while(tmpNode)
				{
					if(xmlStrcmp(tmpNode->name, (const xmlChar*)"event") == 0)
					{
						if(readXMLString(tmpNode, "name", strValue))
							mType->scriptList.push_back(strValue);
						else
							SHOW_XML_WARNING("Missing name for script event");
					}
					tmpNode = tmpNode->next;
				}
			}
			else
				SHOW_XML_WARNING("Unknown attribute type - " << p->name);

			p = p->next;
		}
		xmlFreeDoc(doc);
	}
	else
		monsterLoad = false;

	if(monsterLoad)
	{
		static uint32_t id = 0;
		if(new_mType)
		{
			id++;
			monsterNames[asLowerCaseString(monster_name)] = id;
			monsters[id] = mType;
		}

		return true;
	}
	else
	{
		if(new_mType)
			delete mType;

		return false;
	}
}

bool Monsters::loadLootItem(xmlNodePtr node, LootBlock& lootBlock)
{
	int32_t intValue;
	std::string strValue;

	if(readXMLInteger(node, "id", intValue))
		lootBlock.id = intValue;

	if(lootBlock.id == 0)
		return false;

	if(readXMLInteger(node, "countmax", intValue))
	{
		lootBlock.countmax = intValue;
		if(lootBlock.countmax > 100)
			lootBlock.countmax = 100;
	}
	else
		lootBlock.countmax = 1;

	if(readXMLInteger(node, "chance", intValue) || readXMLInteger(node, "chance1", intValue))
	{
		lootBlock.chance = intValue;
		if(lootBlock.chance > MAX_LOOTCHANCE)
			lootBlock.chance = MAX_LOOTCHANCE;
	}
	else
		lootBlock.chance = MAX_LOOTCHANCE;

	if(Item::items[lootBlock.id].isContainer())
		loadLootContainer(node, lootBlock);

	//optional
	if(readXMLInteger(node, "subtype", intValue))
		lootBlock.subType = intValue;

	if(readXMLInteger(node, "actionId", intValue))
		lootBlock.actionId = intValue;

	if(readXMLInteger(node, "uniqueId", intValue))
		lootBlock.uniqueId = intValue;

	if(readXMLString(node, "text", strValue))
		lootBlock.text = strValue;

	return true;
}

bool Monsters::loadLootContainer(xmlNodePtr node, LootBlock& lBlock)
{
	if(node == NULL)
		return false;

	xmlNodePtr tmpNode = node->children;
	xmlNodePtr p;

	if(tmpNode == NULL)
		return false;

	while(tmpNode)
	{
		if(xmlStrcmp(tmpNode->name, (const xmlChar*)"inside") == 0)
		{
			p = tmpNode->children;
			while(p)
			{
				LootBlock lootBlock;
				if(loadLootItem(p, lootBlock))
					lBlock.childLoot.push_back(lootBlock);
				p = p->next;
			}

			return true;
		}

		tmpNode = tmpNode->next;
	}

	return false;
}

MonsterType* Monsters::getMonsterType(const std::string& name)
{
	uint32_t mId = getIdByName(name);
	if(mId == 0)
		return NULL;

	return getMonsterType(mId);
}

MonsterType* Monsters::getMonsterType(uint32_t mid)
{
	MonsterMap::iterator it = monsters.find(mid);
	if(it != monsters.end())
		return it->second;

	return NULL;
}

uint32_t Monsters::getIdByName(const std::string& name)
{
	std::string tmp = name;
	MonsterNameMap::iterator it = monsterNames.find(asLowerCaseString(tmp));
	if(it != monsterNames.end())
		return it->second;

	return 0;
}

Monsters::~Monsters()
{
	for(MonsterMap::iterator it = monsters.begin(); it != monsters.end(); it++)
		delete it->second;
}
