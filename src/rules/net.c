
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "net.h"
#include "rules.h"
#include "json.h"

#ifdef _WIN32
int asprintf(char** ret, char* format, ...){
	va_list args;
	*ret = NULL;
	if (!format) return 0;
	va_start(args, format);
	int size = _vscprintf(format, args);
	if (size == 0) {
		*ret = (char*)malloc(1);
		**ret = 0;
	}
	else {
		size++; //for null
		*ret = (char*)malloc(size + 2);
		if (*ret) {
			_vsnprintf(*ret, size, format, args);
		}
		else {
			return -1;
		}
	}

	va_end(args);
	return size;
}
#endif

static unsigned int createIdiom(ruleset *tree, jsonValue *newValue, char **idiomString) {
    char *rightProperty;
    char *rightAlias;
    char *valueString;
    idiom *newIdiom;
    switch (newValue->type) {
        case JSON_EVENT_PROPERTY:
            rightProperty = &tree->stringPool[newValue->value.property.nameOffset];
            rightAlias = &tree->stringPool[newValue->value.property.idOffset];
            if (asprintf(idiomString, "frame[\"%s\"][\"%s\"]", rightAlias, rightProperty) == -1) {
                return ERR_OUT_OF_MEMORY;
            }
            break;
        case JSON_EVENT_IDIOM:
            newIdiom = &tree->idiomPool[newValue->value.idiomOffset];
            char *op = "";
            switch (newIdiom->operator) {
                case OP_ADD:
                    op = "+";
                    break;
                case OP_SUB:
                    op = "-";
                    break;
                case OP_MUL:
                    op = "*";
                    break;
                case OP_DIV:
                    op = "/";
                    break;
            }
            
            char *rightIdiomString = NULL;
            unsigned int result = createIdiom(tree, &newIdiom->right, &rightIdiomString);
            if (result != RULES_OK) {
                return result;
            }

            char *leftIdiomString = NULL;
            result = createIdiom(tree, &newIdiom->left, &leftIdiomString);
            if (result != RULES_OK) {
                return result;
            }

            if (asprintf(idiomString, "(%s %s %s)", leftIdiomString, op, rightIdiomString) == -1) {
                return ERR_OUT_OF_MEMORY;
            }
            
            free(rightIdiomString);
            free(leftIdiomString);
            break;
        case JSON_STRING:
            valueString = &tree->stringPool[newValue->value.stringOffset];
            if (asprintf(idiomString, "%s", valueString) == -1) {
                return ERR_OUT_OF_MEMORY;
            }
            break;
        case JSON_INT:
            if (asprintf(idiomString, "%ld", newValue->value.i) == -1) {
                return ERR_OUT_OF_MEMORY;
            }
            break;
        case JSON_DOUBLE:
            if (asprintf(idiomString, "%g", newValue->value.d) == -1) {
                return ERR_OUT_OF_MEMORY;
            }
        case JSON_BOOL:
            if (newValue->value.b == 0) {
                if (asprintf(idiomString, "false") == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            }
            else {
                if (asprintf(idiomString, "true") == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            }
            break;
    }

    return RULES_OK;
}

static unsigned int createTest(ruleset *tree, expression *expr, char **test, char **primaryKey, char **primaryFrameKey) {
    char *comp = NULL;
    char *compStack[32];
    unsigned char compTop = 0;
    unsigned char first = 1;
    unsigned char setPrimaryKey = 0;
    *primaryKey = NULL;
    *primaryFrameKey = NULL;
    if (asprintf(test, "") == -1) {
        return ERR_OUT_OF_MEMORY;
    }
    

    for (unsigned short i = 0; i < expr->termsLength; ++i) {
        unsigned int currentNodeOffset = tree->nextPool[expr->t.termsOffset + i];
        node *currentNode = &tree->nodePool[currentNodeOffset];
        if (currentNode->value.a.operator == OP_AND) {
            char *oldTest = *test;
            if (first) {
                setPrimaryKey = 1;
                if (asprintf(test, "%s(", *test) == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            } else {
                if (asprintf(test, "%s %s (", *test, comp) == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            }
            free(oldTest);
            
            compStack[compTop] = comp;
            ++compTop;
            comp = "and";
            first = 1;
        } else if (currentNode->value.a.operator == OP_OR) {    
            char *oldTest = *test;
            if (first) {
                if (asprintf(test, "%s(", *test) == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            } else {
                setPrimaryKey = 0;
                if (asprintf(test, "%s %s (", *test, comp) == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            }
            free(oldTest); 
            
            compStack[compTop] = comp;
            ++compTop;
            comp = "or";
            first = 1;           
        } else if (currentNode->value.a.operator == OP_END) {
            --compTop;
            comp = compStack[compTop];
            
            char *oldTest = *test;
            if (asprintf(test, "%s)", *test)  == -1) {
                return ERR_OUT_OF_MEMORY;
            }
            free(oldTest);            
        } else {
            char *leftProperty = &tree->stringPool[currentNode->nameOffset];
            char *op = "";
            switch (currentNode->value.a.operator) {
                case OP_LT:
                    op = "<";
                    break;
                case OP_LTE:
                    op = "<=";
                    break;
                case OP_GT:
                    op = ">";
                    break;
                case OP_GTE:
                    op = ">=";
                    break;
                case OP_EQ:
                    op = "==";
                    break;
                case OP_NEQ:
                    op = "~=";
                    break;
            }

            char *idiomString = NULL;
            unsigned int result = createIdiom(tree, &currentNode->value.a.right, &idiomString);
            if (result != RULES_OK) {
                return result;
            }

            char *oldTest = *test;
            if (first) {
                if (asprintf(test, "%smessage[\"%s\"] %s %s", *test, leftProperty, op, idiomString)  == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
                first = 0;
            } else {
                if (asprintf(test, "%s %s message[\"%s\"] %s %s", *test, comp, leftProperty, op, idiomString) == -1) {
                    return ERR_OUT_OF_MEMORY;
                }
            }

            if (setPrimaryKey && currentNode->value.a.operator == OP_EQ) {
                if (*primaryKey == NULL) {
                    if (asprintf(primaryKey, "message[\"%s\"]", leftProperty)  == -1) {
                        return ERR_OUT_OF_MEMORY;
                    }

                    if (asprintf(primaryFrameKey, "%s", idiomString)  == -1) {
                        return ERR_OUT_OF_MEMORY;
                    }
                }
                else {
                    char *oldKey = *primaryKey;
                    if (asprintf(primaryKey, "%s .. message[\"%s\"]", *primaryKey, leftProperty)  == -1) {
                        return ERR_OUT_OF_MEMORY;
                    }
                    free(oldKey);
                    oldKey = *primaryFrameKey;
                    if (asprintf(primaryFrameKey, "%s .. %s", *primaryFrameKey, idiomString)  == -1) {
                        return ERR_OUT_OF_MEMORY;
                    }
                    free(oldKey);
                }
            }

            free(idiomString);
            free(oldTest);
        }
    }

    if (first) {
        free(*test);
        if (asprintf(test, "1")  == -1) {
            return ERR_OUT_OF_MEMORY;
        }
    }

    if (*primaryKey == NULL) {
        if (asprintf(primaryKey, "nil") == -1) {
            return ERR_OUT_OF_MEMORY;
        }
        if (asprintf(primaryFrameKey, "nil") == -1) {
            return ERR_OUT_OF_MEMORY;
        }
    }
   
    return RULES_OK;
}

static unsigned int loadCommands(ruleset *tree, binding *rulesBinding) {
    redisContext *reContext = rulesBinding->reContext;
    redisReply *reply;
    rulesBinding->hashArray = malloc(tree->actionCount * sizeof(functionHash));
    char *name = &tree->stringPool[tree->nameOffset];
    int nameLength = strlen(name);
#ifdef _WIN32
	char *actionKey = (char *)_alloca(sizeof(char)*(nameLength + 3));
	sprintf_s(actionKey, nameLength + 3, "%s!a", name);
#else
    char actionKey[nameLength + 3];
	snprintf(actionKey, nameLength + 3, "%s!a", name);
#endif
    char *lua = NULL;
    char *peekActionLua = NULL;
    char *addMessageLua = NULL;
    if (asprintf(&peekActionLua, "")  == -1) {
        return ERR_OUT_OF_MEMORY;
    }

    if (asprintf(&addMessageLua, "")  == -1) {
        return ERR_OUT_OF_MEMORY;
    }

    for (unsigned int i = 0; i < tree->nodeOffset; ++i) {
        char *oldLua;
        char *oldPeekActionLua;
        node *currentNode = &tree->nodePool[i];
        if (asprintf(&lua, "")  == -1) {
            return ERR_OUT_OF_MEMORY;
        }

        if (currentNode->type == NODE_ACTION) {
            char *packFrameLua = NULL;
            char *unpackFrameLua = NULL;
            char *oldPackFrameLua = NULL;
            char *oldUnpackFrameLua = NULL;
            char *oldAddMessageLua = NULL;
            char *actionName = &tree->stringPool[currentNode->nameOffset];
            char *actionLastName = strchr(actionName, '!');
#ifdef _WIN32
			char *actionAlias = (char *)_alloca(sizeof(char)*(actionLastName - actionName + 1));
#else
			char actionAlias[actionLastName - actionName + 1];
#endif
            
            strncpy(actionAlias, actionName, actionLastName - actionName);
            actionAlias[actionLastName - actionName] = '\0';

            for (unsigned int ii = 0; ii < currentNode->value.c.joinsLength; ++ii) {
                unsigned int currentJoinOffset = tree->nextPool[currentNode->value.c.joinsOffset + ii];
                join *currentJoin = &tree->joinPool[currentJoinOffset];
                
                oldPeekActionLua = peekActionLua;
                if (asprintf(&peekActionLua, 
"%sreviewers = {}\n"
"reviewers_directory[\"%s!%d!r!\"] = reviewers\n"
"keys = {}\n"
"keys_directory[\"%s!%d!r!\"] = keys\n"
"primary_frame_keys = {}\n"
"primary_frame_keys_directory[\"%s!%d!r!\"] = primary_frame_keys\n",
                            peekActionLua,
                            actionName,
                            ii,
                            actionName,
                            ii,
                            actionName,
                            ii)  == -1) {
                    return ERR_OUT_OF_MEMORY;
                }  
                free(oldPeekActionLua);
                for (unsigned int iii = 0; iii < currentJoin->expressionsLength; ++iii) {
                    unsigned int expressionOffset = tree->nextPool[currentJoin->expressionsOffset + iii];
                    expression *expr = &tree->expressionPool[expressionOffset];
                    char *currentAlias = &tree->stringPool[expr->aliasOffset];
                    char *currentKey = &tree->stringPool[expr->nameOffset];
                    oldLua = lua;

                    if (iii == 0) {
                        oldAddMessageLua = addMessageLua;
                        if (asprintf(&addMessageLua, 
"%sprimary_message_keys[\"%s\"] = function(message)\n"
"    return nil\n"
"end\n",
                                    addMessageLua,
                                    currentKey)  == -1) {
                            return ERR_OUT_OF_MEMORY;
                        }  
                        free(oldAddMessageLua);

                        if (expr->not) { 
                            if (asprintf(&packFrameLua,
"    result[1] = \"$n\"\n")  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }

                            if (asprintf(&unpackFrameLua,
"    result[\"%s\"] = \"$n\"\n",
                                        currentAlias)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }

                            if (asprintf(&lua, 
"%skeys = {}\n"
"directory = {[\"0\"] = 1}\n"
"reviewers = {}\n"
"frame_packers = {}\n"
"frame_unpackers = {}\n"
"results_key = \"%s!%d!r!\" .. sid\n"                   
"keys[1] = \"%s\"\n"
"inverse_directory = {[1] = true}\n"
"primary_frame_keys = {}\n"
"primary_message_keys = {}\n"
"directory[\"%s\"] = 1\n"
"reviewers[1] = function(message, frame, index)\n"
"    if not message then\n"
"        frame[\"%s\"] = \"$n\"\n"
"        return true\n"
"    end\n"
"    return false\n"
"end\n"
"frame_packers[1] = function(frame, full_encode)\n"
"    local result = {}\n%s"
"    return cmsgpack.pack(result)\n"
"end\n"
"frame_unpackers[1] = function(packed_frame)\n"
"    local frame = cmsgpack.unpack(packed_frame)\n"
"    local result = {}\n%s"
"    return result\n"
"end\n"
"primary_message_keys[1] = function(message)\n"
"    return nil\n"
"end\n"
"primary_frame_keys[1] = function(frame)\n"
"    return nil\n"
"end\n",
                                         lua,
                                         actionName,
                                         ii,
                                         currentKey,
                                         currentKey,
                                         currentAlias,
                                         packFrameLua,
                                         unpackFrameLua)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                            oldPeekActionLua = peekActionLua;
                            if (asprintf(&peekActionLua, 
"%skeys[1] = \"%s\"\n"
"reviewers[1] = function(message, frame, index)\n"
"    if not message then\n"
"        return true\n"
"    end\n"
"    return false\n"
"end\n"
"primary_frame_keys[1] = function(frame)\n"
"    return nil\n"
"end\n",
                                         peekActionLua,
                                         currentKey)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }  
                            free(oldPeekActionLua);
                        // not (expr->not)
                        } else {
                            if (asprintf(&packFrameLua,
"    message = frame[\"%s\"]\n"
"    if full_encode and not message[\"$f\"] then\n"
"        result[1] = message\n"
"    else\n"
"        result[1] = message[\"id\"]\n"
"    end\n",
                                        currentAlias)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }

                            if (asprintf(&unpackFrameLua,
"    message = fetch_message(frame[1])\n"
"    if not message then\n"
"        return nil\n"
"    end\n"
"    result[\"%s\"] = message\n",
                                        currentAlias)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }

                            if (asprintf(&lua, 
"%skeys = {}\n"
"directory = {[\"0\"] = 1}\n"
"reviewers = {}\n"
"frame_packers = {}\n"
"frame_unpackers = {}\n"
"results_key = \"%s!%d!r!\" .. sid\n"                   
"keys[1] = \"%s\"\n"
"inverse_directory = {}\n"
"primary_frame_keys = {}\n"
"primary_message_keys = {}\n"
"directory[\"%s\"] = 1\n"
"reviewers[1] = function(message, frame, index)\n"
"    if message then\n"
"        frame[\"%s\"] = message\n"
"        return true\n"
"    end\n"
"    return false\n"
"end\n"
"frame_packers[1] = function(frame, full_encode)\n"
"    local result = {}\n"
"    local message\n%s"
"    return cmsgpack.pack(result)\n"
"end\n"
"frame_unpackers[1] = function(packed_frame)\n"
"    local frame = cmsgpack.unpack(packed_frame)\n"
"    local result = {}\n"
"    local message\n%s"
"    return result\n"
"end\n"
"primary_message_keys[1] = function(message)\n"
"    return nil\n"
"end\n"
"primary_frame_keys[1] = function(frame)\n"
"    return nil\n"
"end\n",
                                         lua,
                                         actionName,
                                         ii,
                                         currentKey,
                                         currentKey,
                                         currentAlias,
                                         packFrameLua,
                                         unpackFrameLua)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                        }
                    // not (iii == 0)
                    } else {
                        char *test = NULL;
                        char *primaryKeyLua = NULL;
                        char *primaryFrameKeyLua = NULL;
                        unsigned int result = createTest(tree, expr, &test, &primaryKeyLua, &primaryFrameKeyLua);
                        if (result != RULES_OK) {
                            return result;
                        }

                        oldAddMessageLua = addMessageLua;
                        if (asprintf(&addMessageLua, 
"%sprimary_message_keys[\"%s\"] = function(message)\n"
"    return %s\n"
"end\n",
                                    addMessageLua,
                                    currentKey, 
                                    primaryKeyLua)  == -1) {
                            return ERR_OUT_OF_MEMORY;
                        }  
                        free(oldAddMessageLua);

                        if (expr->not) { 
                            oldPackFrameLua = packFrameLua;
                            if (asprintf(&packFrameLua,
"%s    result[%d] = \"$n\"\n",
                                         packFrameLua,
                                         iii + 1)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                            free(oldPackFrameLua);

                            oldUnpackFrameLua = unpackFrameLua;
                            if (asprintf(&unpackFrameLua,
"%s    result[\"%s\"] = \"$n\"\n",
                                        unpackFrameLua,
                                        currentAlias)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                            free(oldUnpackFrameLua);

                            if (asprintf(&lua,
"%skeys[%d] = \"%s\"\n"
"inverse_directory[%d] = true\n"
"directory[\"%s\"] = %d\n"
"reviewers[%d] = function(message, frame, index)\n"
"    if not message or not (%s) then\n"
"        frame[\"%s\"] = \"$n\"\n"
"        return true\n"
"    end\n"
"    return false\n"
"end\n"
"frame_packers[%d] = function(frame, full_encode)\n"
"    local result = {}\n"
"    local message\n%s"
"    return cmsgpack.pack(result)\n"
"end\n"
"frame_unpackers[%d] = function(packed_frame)\n"
"    local frame = cmsgpack.unpack(packed_frame)\n"
"    local result = {}\n"
"    local message\n%s"
"    return result\n"
"end\n"
"primary_message_keys[%d] = function(message)\n"
"    return %s\n"
"end\n"
"primary_frame_keys[%d] = function(frame)\n"
"    return %s\n"
"end\n",
                                         lua,
                                         iii + 1, 
                                         currentKey,
                                         iii + 1, 
                                         currentKey,
                                         iii + 1,
                                         iii + 1,
                                         test,
                                         currentAlias,
                                         iii + 1,
                                         packFrameLua,
                                         iii + 1,
                                         unpackFrameLua,
                                         iii + 1,
                                         primaryKeyLua,
                                         iii + 1,
                                         primaryFrameKeyLua)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }

                            oldPeekActionLua = peekActionLua;
                            if (asprintf(&peekActionLua, 
"%skeys[%d] = \"%s\"\n"
"reviewers[%d] = function(message, frame, index)\n"
"    if not message or not (%s) then\n"
"        return true\n"
"    end\n"
"    return false\n"
"end\n"
"primary_frame_keys[%d] = function(frame)\n"
"    return %s\n"
"end\n",
                                         peekActionLua,
                                         iii + 1, 
                                         currentKey,
                                         iii + 1,
                                         test,
                                         iii + 1,
                                         primaryFrameKeyLua)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }  
                            free(oldPeekActionLua);

                        // not (expr->not)
                        } else {
                            oldPackFrameLua = packFrameLua;
                            if (asprintf(&packFrameLua,
"%s    message = frame[\"%s\"]\n"
"    if full_encode and not message[\"$f\"] then\n"
"        result[%d] = message\n"
"    else\n"
"        result[%d] = message[\"id\"]\n"
"    end\n",
                                         packFrameLua,
                                         currentAlias,
                                         iii + 1,
                                         iii + 1)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                            free(oldPackFrameLua);

                            oldUnpackFrameLua = unpackFrameLua;
                            if (asprintf(&unpackFrameLua,
"%s    message = fetch_message(frame[%d])\n"
"    if not message then\n"
"        return nil\n"
"    end\n"
"    result[\"%s\"] = message\n",
                                         unpackFrameLua,
                                         iii + 1,
                                         currentAlias)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                            free(oldUnpackFrameLua);

                            if (asprintf(&lua,
"%skeys[%d] = \"%s\"\n"
"directory[\"%s\"] = %d\n"
"reviewers[%d] = function(message, frame, index)\n"
"    if message and %s then\n"
"        frame[\"%s\"] = message\n"
"        return true\n"
"    end\n"
"    return false\n"
"end\n"
"frame_packers[%d] = function(frame, full_encode)\n"
"    local result = {}\n"
"    local message\n%s"
"    return cmsgpack.pack(result)\n"
"end\n"
"frame_unpackers[%d] = function(packed_frame)\n"
"    local frame = cmsgpack.unpack(packed_frame)\n"
"    local result = {}\n"
"    local message\n%s"
"    return result\n"
"end\n"
"primary_message_keys[%d] = function(message)\n"
"    return %s\n"
"end\n"
"primary_frame_keys[%d] = function(frame)\n"
"    return %s\n"
"end\n",
                                         lua,
                                         iii + 1, 
                                         currentKey,
                                         currentKey,
                                         iii + 1,
                                         iii + 1,
                                         test,
                                         currentAlias,
                                         iii + 1,
                                         packFrameLua,
                                         iii + 1,
                                         unpackFrameLua,
                                         iii + 1,
                                         primaryKeyLua,
                                         iii + 1,
                                         primaryFrameKeyLua)  == -1) {
                                return ERR_OUT_OF_MEMORY;
                            }
                        // done not (expr->not)
                        }
                        free(test);
                        free(primaryKeyLua);
                        free(primaryFrameKeyLua);
                    // done not (iii == 0)
                    }

                    free(oldLua);
                }

                oldPeekActionLua = peekActionLua;
                if (asprintf(&peekActionLua, 
"%sframe_restore_directory[\"%s!%d!r!\"] = function(frame, result)\n"
"    local message\n%s"
"    return true\n"
"end\n",
                             peekActionLua,
                             actionName,
                             ii,
                             unpackFrameLua)  == -1) {
                    return ERR_OUT_OF_MEMORY;
                }  
                free(oldPeekActionLua);

                oldLua = lua;
                if (currentNode->value.c.span > 0)
                {
                    if (asprintf(&lua,
"%sprocess_key(message, nil, %d)\n",
                                 lua,
                                 currentNode->value.c.span)  == -1) {
                        return ERR_OUT_OF_MEMORY;
                    }
                } else {
                    if (asprintf(&lua,
"%sprocess_key(message, %d, nil)\n",
                                 lua,
                                 currentNode->value.c.count)  == -1) {
                        return ERR_OUT_OF_MEMORY;
                    }
                }

                free(oldLua);
            }

            free(unpackFrameLua);
            free(packFrameLua);
            oldLua = lua;
            if (asprintf(&lua,
"local key = ARGV[1]\n"
"local sid = ARGV[2]\n"
"local mid = ARGV[3]\n"
"local score = tonumber(ARGV[4])\n"
"local assert_fact = tonumber(ARGV[5])\n"
"local events_hashset = \"%s!e!\" .. sid\n"
"local facts_hashset = \"%s!f!\" .. sid\n"
"local visited_hashset = \"%s!v!\" .. sid\n"
"local actions_key = \"%s!a\"\n"
"local facts_message_cache = {}\n"
"local events_message_cache = {}\n"
"local facts_mids_cache = {}\n"
"local events_mids_cache = {}\n"
"local keys\n"
"local directory\n"
"local reviewers\n"
"local frame_packers\n"
"local frame_unpackers\n"
"local results_key\n"
"local inverse_directory\n"
"local primary_message_keys\n"
"local primary_frame_keys\n"
"local results = {}\n"
"local cleanup_mids = function(index, frame, events_key, messages_key, mids_cache, message_cache)\n"
"    local event_mids = mids_cache[events_key]\n"
"    local primary_key = primary_frame_keys[index](frame)\n"
"    local new_mids = nil\n"
"    if not primary_key then\n"
"        new_mids = event_mids\n"
"    else\n"
"        new_mids = event_mids[primary_key]\n"
"    end\n"
"    local result_mids = {}\n"
"    for i = 1, #new_mids, 1 do\n"
"        local new_mid = new_mids[i]\n"
"        if message_cache[new_mid] then\n"
"            table.insert(result_mids, new_mid)\n"
"        end\n"
"    end\n"
"    if primary_key then\n"
"        event_mids[primary_key] = result_mids\n"
"        redis.call(\"hset\", events_key, primary_key, cmsgpack.pack(result_mids))\n"
"    else\n"
"        mids_cache[events_key] = result_mids\n"
"        redis.call(\"set\", events_key, cmsgpack.pack(result_mids))\n"
"    end\n"
"end\n"
"local get_mids = function(index, frame, events_key, messages_key, mids_cache, message_cache)\n"
"    local event_mids = mids_cache[events_key]\n"
"    local primary_key = primary_frame_keys[index](frame)\n"
"    local new_mids = nil\n"
"    if not event_mids then\n"
"        event_mids = {}\n"
"        mids_cache[events_key] = event_mids\n"
"    elseif not primary_key then\n"
"        new_mids = event_mids\n"
"    else\n"
"        new_mids = event_mids[primary_key]\n"
"    end\n"
"    if new_mids then\n"
"        return new_mids\n"
"    else\n"
"        local packed_message_list\n"
"        if primary_key then\n"
"            packed_message_list = redis.call(\"hget\", events_key, primary_key)\n"
"        else\n"
"            packed_message_list = redis.call(\"get\", events_key)\n"
"        end\n"
"        if packed_message_list then\n"
"            new_mids = cmsgpack.unpack(packed_message_list)\n"
"redis.call(\"rpush\", \"debug\", \"load \" .. actions_key .. \" \" .. keys[index] .. \" \" .. #new_mids)\n"
"        else\n"
"            new_mids = {}\n"
"        end\n"
"    end\n"
"    if primary_key then\n"
"        event_mids[primary_key] = new_mids\n"
"    else\n"
"        mids_cache[events_key] = new_mids\n"
"    end\n"
"    return new_mids\n"
"end\n"
"local get_message = function(new_mid, messages_key, message_cache)\n"
"    local message = false\n"
"    if message_cache[new_mid] ~= nil then\n"
"        message = message_cache[new_mid]\n"
"    else\n"
"        local packed_message = redis.call(\"hget\", messages_key, new_mid)\n"
"        if packed_message then\n"
"            message = cmsgpack.unpack(packed_message)\n"
"        end\n"
"        message_cache[new_mid] = message\n"
"    end\n"
"    return message\n"
"end\n"
"local fetch_message = function(new_mid)\n"
"    local message = get_message(new_mid, events_hashset, events_message_cache)\n"
"    if not message then\n"
"        message = get_message(new_mid, facts_hashset, facts_message_cache)\n"
"    end\n"
"    return message\n"
"end\n"
"local save_message = function(index, message, events_key, messages_key)\n"
"    redis.call(\"hsetnx\", messages_key, message[\"id\"], cmsgpack.pack(message))\n"
"    local primary_key = primary_message_keys[index](message)\n"
"    local packed_message_list\n"
"    if primary_key then\n"
"        packed_message_list = redis.call(\"hget\", events_key, primary_key)\n"
"    else\n"
"        packed_message_list = redis.call(\"get\", events_key)\n"
"    end\n"
"    local message_list = {}\n"
"    if packed_message_list then\n"
"        message_list = cmsgpack.unpack(packed_message_list)\n"
"    end\n"
"    table.insert(message_list, message[\"id\"])\n"
"    if primary_key then\n"
"        redis.call(\"hset\", events_key, primary_key, cmsgpack.pack(message_list))\n"
"    else\n"
"        redis.call(\"set\", events_key, cmsgpack.pack(message_list))\n"
"    end\n"
"end\n"
"local save_result = function(frame, index)\n"
"    table.insert(results, 1, frame_packers[index](frame, true))\n"
"    for name, message in pairs(frame) do\n"
"        if message ~= \"$n\" and not message[\"$f\"] then\n"
"            redis.call(\"hdel\", events_hashset, message[\"id\"])\n"
"            events_message_cache[message[\"id\"]] = false\n"
"        end\n"
"    end\n"
"end\n"
"local is_pure_fact = function(frame, index)\n"
"    local message_count = 0\n"
"    for name, message in pairs(frame) do\n"
"        if message ~= 1 and message[\"$f\"] ~= 1 then\n" 
"           return false\n"    
"        end\n"
"        message_count = message_count + 1\n"   
"    end\n"    
"    return (message_count == index - 1)\n"
"end\n"
"local process_frame\n"
"local process_event_and_frame = function(message, frame, index, use_facts)\n"
"    local result = 0\n"
"    local new_frame = {}\n"
"    for name, new_message in pairs(frame) do\n"
"        new_frame[name] = new_message\n"
"    end\n"
"    if reviewers[index](message, new_frame, index) then\n"
"        if (index == #reviewers) then\n"
"            save_result(new_frame, index)\n"
"            return 1\n"
"        else\n"
"            result = process_frame(new_frame, index + 1, use_facts)\n"
"            if result == 0 or use_facts then\n"
"                local frames_key\n"
"                local primary_key = primary_frame_keys[index + 1](new_frame)\n"
"                if primary_key then\n"
"                    frames_key = keys[index + 1] .. \"!c!\" .. sid .. \"!\" .. primary_key\n"
"                else\n"
"                    frames_key = keys[index + 1] .. \"!c!\" .. sid\n"
"                end\n"
"                redis.call(\"rpush\", frames_key, frame_packers[index](new_frame))\n"
"            end\n"
"        end\n"
"    end\n"
"    return result\n"
"end\n"
"local process_frame_for_key = function(frame, index, events_key, use_facts)\n"
"    local result = nil\n"
"    local inverse = inverse_directory[index]\n"
"    local messages_key = events_hashset\n"
"    local message_cache = events_message_cache\n"
"    local mids_cache = events_mids_cache\n"
"    local cleanup = false\n"
"    if use_facts then\n"
"       messages_key = facts_hashset\n"
"       message_cache = facts_message_cache\n"
"       mids_cache = facts_mids_cache\n"
"    end\n"
"    if inverse then\n"
"        local new_frame = {}\n"
"        for name, new_message in pairs(frame) do\n"
"            new_frame[name] = new_message\n"
"        end\n"
"        local new_mids = get_mids(index, frame, events_key, messages_key, mids_cache, message_cache)\n"
"        for i = 1, #new_mids, 1 do\n"
"            local message = get_message(new_mids[i], messages_key, message_cache)\n"
"            if not message then\n"
"                cleanup = true\n"
"            elseif not reviewers[index](message, new_frame, index) then\n"
"                local frames_key\n"
"                local primary_key = primary_frame_keys[index](new_frame)\n"
"                if primary_key then\n"
"                    frames_key = keys[index] .. \"!i!\" .. sid .. \"!\" .. primary_key\n"
"                else\n"
"                    frames_key = keys[index] .. \"!i!\" .. sid\n"
"                end\n"
"                redis.call(\"rpush\", frames_key, frame_packers[index - 1](new_frame))\n"
"                result = 0\n"
"                break\n"
"            end\n"
"        end\n"
"    else\n"
"        local new_mids = get_mids(index, frame, events_key, messages_key, mids_cache, message_cache)\n"
"        for i = 1, #new_mids, 1 do\n"
"            local message = get_message(new_mids[i], messages_key, message_cache)\n"
"            if not message then\n"
"                cleanup = true\n"
"            else\n"
"                local count = process_event_and_frame(message, frame, index, use_facts)\n"
"                if not result then\n"
"                    result = 0\n"
"                end\n"
"                result = result + count\n"
"                if not is_pure_fact(frame, index) then\n"
"                    break\n"
"                end\n"
"            end\n"
"        end\n"
"    end\n"
"    if cleanup then\n"
"        cleanup_mids(index, frame, events_key, messages_key, mids_cache, message_cache)\n"
"    end\n"
"    return result\n"
"end\n"
"process_frame = function(frame, index, use_facts)\n"
"    local first_result = process_frame_for_key(frame, index, keys[index] .. \"!e!\" .. sid, false)\n"
"    local second_result = process_frame_for_key(frame, index, keys[index] .. \"!f!\" .. sid, true)\n"
"    if not first_result and not second_result then\n"
"        return process_event_and_frame(nil, frame, index, use_facts)\n"
"    elseif not first_result then\n"
"        return second_result\n"
"    elseif not second_result then\n"
"        return first_result\n"
"    else\n"
"        return first_result + second_result\n"
"    end\n"
"end\n"
"local process_inverse_event = function(message, index, events_key, use_facts)\n"
"    local result = 0\n"
"    local messages_key = events_hashset\n"
"    if use_facts then\n"
"        messages_key = facts_hashset\n"
"    end\n"
"    redis.call(\"hdel\", messages_key, mid)\n"
"    if index == 1 then\n"
"        result = process_frame({}, 1, use_facts)\n"
"    else\n"
"        local frames_key\n"
"        local primary_key = primary_message_keys[index](message)\n"
"        if primary_key then\n"
"            frames_key = keys[index] .. \"!i!\" .. sid .. \"!\" .. primary_key\n"
"        else\n"
"            frames_key = keys[index] .. \"!i!\" .. sid\n"
"        end\n"
"        local packed_frames_len = redis.call(\"llen\", frames_key)\n"
"        for i = 1, packed_frames_len, 1 do\n"
"            local packed_frame = redis.call(\"rpop\", frames_key)\n"
"            local frame = frame_unpackers[index - 1](packed_frame)\n"
"            if frame then\n"
"                result = result + process_frame(frame, index, use_facts)\n"
"            end\n"     
"        end\n"
"    end\n"
"    return result\n"
"end\n"
"local process_event = function(message, index, events_key, use_facts)\n"
"    local result = 0\n"
"    local messages_key = events_hashset\n"
"    if use_facts then\n"
"        messages_key = facts_hashset\n"
"    end\n"
"    if index == 1 then\n"
"        result = process_event_and_frame(message, {}, 1, use_facts)\n"
"    else\n"
"        local frames_key\n"
"        local primary_key = primary_message_keys[index](message)\n"
"        if primary_key then\n"
"            frames_key = keys[index] .. \"!c!\" .. sid .. \"!\" .. primary_key\n"
"        else\n"
"            frames_key = keys[index] .. \"!c!\" .. sid\n"
"        end\n"
"        local packed_frames_len = redis.call(\"llen\", frames_key)\n"
"        for i = 1, packed_frames_len, 1 do\n"
"            local packed_frame = redis.call(\"rpop\", frames_key)\n"
"            local frame = frame_unpackers[index - 1](packed_frame)\n"
"            if frame then\n"
"                local count = process_event_and_frame(message, frame, index, use_facts)\n"
"                result = result + count\n"         
"                if count == 0 or use_facts then\n"
"                    redis.call(\"lpush\", frames_key, packed_frame)\n"
"                else\n"
"                    break\n" 
"                end\n"
"            end\n"     
"        end\n"
"    end\n"
"    if result == 0 or use_facts then\n"
"        save_message(index, message, events_key, messages_key)\n"
"    end\n"
"    return result\n"
"end\n"
"local process_key = function(message, window, span)\n"
"    local index = directory[key]\n"
"    if index then\n"
"        if span then\n"
"            local last_score = redis.call(\"get\", results_key .. \"!d\")\n"
"            if not last_score then\n"
"                redis.call(\"set\", results_key .. \"!d\", score)\n"
"            else\n"
"                local new_score = last_score + span\n"
"                if score > new_score then\n"
"                    redis.call(\"rpush\", results_key, 0)\n"
"                    redis.call(\"rpush\", actions_key .. \"!\" .. sid, results_key)\n"
"                    redis.call(\"rpush\", actions_key .. \"!\" .. sid, 0)\n"
"                    redis.call(\"zadd\", actions_key , score, sid)\n"
"                    local span_count, span_remain = math.modf((score - new_score) / span)\n"
"                    last_score = new_score + span_count * span\n"
"                    redis.call(\"set\", results_key .. \"!d\", last_score)\n"
"                end\n"    
"            end\n"
"        end\n"
"        local count = 0\n"
"        if not message then\n"
"            if assert_fact == 0 then\n"
"                count = process_inverse_event(message, index, keys[index] .. \"!e!\" .. sid, false)\n"
"            else\n"
"                count = process_inverse_event(message, index, keys[index] .. \"!f!\" .. sid, true)\n"
"            end\n"
"        else\n"
"            if assert_fact == 0 then\n"
"                count = process_event(message, index, keys[index] .. \"!e!\" .. sid, false)\n"
"            else\n"
"                count = process_event(message, index, keys[index] .. \"!f!\" .. sid, true)\n"
"            end\n"
"        end\n"
"        if (count > 0) then\n"
"            if span then\n"
"                for i = 1, #results, 1 do\n"
"                    redis.call(\"rpush\", results_key, results[i])\n"
"                end\n"
"            else\n"
"                for i = #results, 1, -1 do\n"
"                    redis.call(\"lpush\", results_key, results[i])\n"
"                end\n"
"                local diff\n"
"                if window < 10 then"
"                    local length = redis.call(\"llen\", results_key)\n"
"                    local prev_count, prev_remain = math.modf((length - count) / window)\n"
"                    local new_count, prev_remain = math.modf(length / window)\n"
"                    diff = new_count - prev_count\n"
"                    if diff > 0 then\n"
"                        for i = 0, diff - 1, 1 do\n"
"                            redis.call(\"rpush\", actions_key .. \"!\" .. sid, results_key)\n"
"                            redis.call(\"rpush\", actions_key .. \"!\" .. sid, window)\n"
"                        end\n"
"                        redis.call(\"zadd\", actions_key , score, sid)\n"
"                    end\n"
"                else\n"
"                    local new_count, new_remain = math.modf(#results / window)\n"
"                    local new_remain = #results %% window\n"
"                    if new_count > 0 then\n"
"                        for i = 1, new_count, 1 do\n"
"                            redis.call(\"rpush\", actions_key .. \"!\" .. sid, results_key)\n"
"                            redis.call(\"rpush\", actions_key .. \"!\" .. sid, window)\n"
"                        end\n"
"                    end\n"
"                    if new_remain > 0 then\n"
"                        redis.call(\"rpush\", actions_key .. \"!\" .. sid, results_key)\n"
"                        redis.call(\"rpush\", actions_key .. \"!\" .. sid, new_remain)\n"
"                    end\n"
"                    if new_count > 0 or new_remain > 0 then\n"
"                        redis.call(\"zadd\", actions_key , score, sid)\n"
"                    end\n"
"                end\n"
"            end\n"
"        end\n"
"    end\n"
"end\n"
"local message = nil\n"
"if #ARGV > 5 then\n"
"    message = {}\n"
"    for index = 6, #ARGV, 3 do\n"
"        if ARGV[index + 2] == \"1\" then\n"
"            message[ARGV[index]] = ARGV[index + 1]\n"
"        elseif ARGV[index + 2] == \"2\" or  ARGV[index + 2] == \"3\" then\n"
"            message[ARGV[index]] = tonumber(ARGV[index + 1])\n"
"        elseif ARGV[index + 2] == \"4\" then\n"
"            if ARGV[index + 1] == \"true\" then\n"
"                message[ARGV[index]] = true\n"
"            else\n"
"                message[ARGV[index]] = false\n"
"            end\n"
"        end\n"
"    end\n"
"    if assert_fact == 1 then\n"
"        message[\"$f\"] = 1\n"
"    end\n"
"end\n"
"if redis.call(\"hsetnx\", visited_hashset, mid, 1) == 0 then\n"
"    if assert_fact == 0 then\n"
"        if message and not redis.call(\"hget\", events_hashset, mid) then\n"
"            return false\n"
"        end\n"
"    else\n"
"        if message and not redis.call(\"hget\", facts_hashset, mid) then\n"
"            return false\n"
"        end\n"
"    end\n"
"end\n%s"
"return true\n",
                         name,
                         name,
                         name,
                         name,
                         lua)  == -1) {
                return ERR_OUT_OF_MEMORY;
            }
            free(oldLua);
            redisAppendCommand(reContext, "SCRIPT LOAD %s", lua);
            redisGetReply(reContext, (void**)&reply);
            if (reply->type == REDIS_REPLY_ERROR) {
                printf("%s\n", reply->str);
                freeReplyObject(reply);
                free(lua);
                return ERR_REDIS_ERROR;
            }

            functionHash *currentAssertHash = &rulesBinding->hashArray[currentNode->value.c.index];
            strncpy(*currentAssertHash, reply->str, HASH_LENGTH);
            (*currentAssertHash)[HASH_LENGTH] = '\0';
            freeReplyObject(reply);
            free(lua);
        }
    }

    if (asprintf(&lua, 
"local facts_key = \"%s!f!\"\n"
"local events_key = \"%s!e!\"\n"
"local action_key = \"%s!a\"\n"
"local state_key = \"%s!s\"\n"
"local timers_key = \"%s!t\"\n"
"local keys_directory = {}\n"
"local keys\n"
"local reviewers_directory = {}\n"
"local reviewers\n"
"local primary_frame_keys_directory = {}\n"
"local primary_frame_keys\n"
"local frame_restore_directory = {}\n"
"local facts_hashset\n"
"local events_hashset\n"
"local events_message_cache = {}\n"
"local facts_message_cache = {}\n"
"local facts_mids_cache = {}\n"
"local events_mids_cache = {}\n"
"local get_mids = function(index, frame, events_key, messages_key, mids_cache, message_cache)\n"
"    local event_mids = mids_cache[events_key]\n"
"    local primary_key = primary_frame_keys[index](frame)\n"
"    local new_mids = nil\n"
"    if not event_mids then\n"
"        event_mids = {}\n"
"        mids_cache[events_key] = event_mids\n"
"    elseif not primary_key then\n"
"        new_mids = event_mids\n"
"    else\n"
"        new_mids = event_mids[primary_key]\n"
"    end\n"
"    if new_mids then\n"
"        return new_mids\n"
"    else\n"
"        local packed_message_list\n"
"        if primary_key then\n"
"            packed_message_list = redis.call(\"hget\", events_key, primary_key)\n"
"        else\n"
"            packed_message_list = redis.call(\"get\", events_key)\n"
"        end\n"
"        if packed_message_list then\n"
"            new_mids = cmsgpack.unpack(packed_message_list)\n"
"        else\n"
"            new_mids = {}\n"
"        end\n"
"    end\n"
"    if primary_key then\n"
"        event_mids[primary_key] = new_mids\n"
"    else\n"
"        mids_cache[events_key] = new_mids\n"
"    end\n"
"    return new_mids\n"
"end\n"
"local get_message = function(new_mid, messages_key, message_cache)\n"
"    local message = false\n"
"    if message_cache[new_mid] ~= nil then\n"
"        message = message_cache[new_mid]\n"
"    else\n"
"        local packed_message = redis.call(\"hget\", messages_key, new_mid)\n"
"        if packed_message then\n"
"            message = cmsgpack.unpack(packed_message)\n"
"        end\n"
"        message_cache[new_mid] = message\n"
"    end\n"
"    return message\n"
"end\n"
"local fetch_message = function(new_mid)\n"
"    if type(new_mid) == \"table\" then\n"
"        return new_mid\n"
"    end\n"
"    return get_message(new_mid, facts_hashset, facts_message_cache)\n"
"end\n"
"local validate_frame_for_key = function(frame, index, events_list_key, messages_key, mids_cache, message_cache)\n"
"    local new_mids = get_mids(index, frame, events_list_key, messages_key, mids_cache, message_cache)\n"
"    for i = 1, #new_mids, 1 do\n"
"        local message = get_message(new_mids[i], messages_key, message_cache)\n"
"        if message and not reviewers[index](message, frame, index) then\n"
"            return false\n"
"        end\n"
"    end\n"
"    return true\n"
"end\n"
"local validate_frame = function(frame, index, sid)\n"
"    local first_result = validate_frame_for_key(frame, index, keys[index] .. \"!e!\" .. sid, events_hashset, events_mids_cache, events_message_cache)\n"
"    local second_result = validate_frame_for_key(frame, index, keys[index] ..\"!f!\" .. sid, facts_hashset, facts_mids_cache, facts_message_cache)\n"
"    return first_result and second_result\n"
"end\n"
"local review_frame = function(frame, rule_action_key, sid, max_score)\n"
"    local indexes = {}\n"
"    local action_id = string.sub(rule_action_key, 1, (string.len(sid) + 1) * -1)\n"
"    local frame_restore = frame_restore_directory[action_id]\n"
"    local full_frame = {}\n"
"    local cancel = false\n"
"    events_hashset = events_key .. sid\n"
"    facts_hashset = facts_key .. sid\n"
"    keys = keys_directory[action_id]\n"
"    reviewers = reviewers_directory[action_id]\n"
"    primary_frame_keys = primary_frame_keys_directory[action_id]\n"
"    if not frame_restore(frame, full_frame) then\n"
"        cancel = true\n"
"    else\n"
"        for i = 1, #frame, 1 do\n"
"            if frame[i] == \"$n\" then\n"
"                if not validate_frame(full_frame, i, sid) then\n"
"                    cancel = true\n"
"                    break\n"
"                end\n"
"            end\n"
"        end\n"
"    end\n"
"    if cancel then\n"
"        for i = 1, #frame, 1 do\n"
"            if type(frame[i]) == \"table\" then\n"
"                redis.call(\"hsetnx\", events_hashset, frame[i][\"id\"], cmsgpack.pack(frame[i]))\n"
"                redis.call(\"zadd\", timers_key, max_score, cjson.encode(frame[i]))\n"
"            end\n"
"        end\n"
"        full_frame = nil\n"
"    end\n"
"    return full_frame\n"
"end\n"
"local load_frame_from_rule = function(rule_action_key, count, sid, max_score)\n"
"    local frames = {}\n"
"    local packed_frames = {}\n"
"    if count == 0 then\n"
"        local packed_frame = redis.call(\"lpop\", rule_action_key)\n"
"        while packed_frame ~= \"0\" do\n"
"            local frame = review_frame(cmsgpack.unpack(packed_frame), rule_action_key, sid, max_score)\n"
"            if frame then\n"
"                table.insert(frames, frame)\n"
"                table.insert(packed_frames, packed_frame)\n"
"            end\n"
"            packed_frame = redis.call(\"lpop\", rule_action_key)\n"
"        end\n"
"        if #packed_frames > 0 then\n"
"            redis.call(\"lpush\", rule_action_key, 0)\n"
"        end\n"
"    else\n"
"        while count > 0 do\n"
"            local packed_frame = redis.call(\"rpop\", rule_action_key)\n"
"            if not packed_frame then\n"
"                break\n"
"            else\n"
"                local frame = review_frame(cmsgpack.unpack(packed_frame), rule_action_key, sid, max_score)\n"
"                if frame then\n"
"                    table.insert(frames, frame)\n"
"                    table.insert(packed_frames, packed_frame)\n"
"                    count = count - 1\n"
"                end\n"
"            end\n"
"        end\n"
"    end\n"
"    for i = #packed_frames, 1, -1 do\n"
"        redis.call(\"lpush\", rule_action_key, packed_frames[i])\n"
"    end\n"
"    if #packed_frames == 0 then\n"
"        return nil, nil\n"
"    end\n"
"    local last_name = string.find(rule_action_key, \"!\") - 1\n"
"    if #frames == 1 and count == 0 then\n"
"        return string.sub(rule_action_key, 1, last_name), frames[1]\n"
"    else\n"
"        return string.sub(rule_action_key, 1, last_name), frames\n"
"    end\n"
"end\n"
"local load_frame_from_sid = function(sid, max_score)\n"
"    local action_list = action_key .. \"!\" .. sid\n"
"    local rule_action_key = redis.call(\"lpop\", action_list)\n"
"    local count = tonumber(redis.call(\"lpop\", action_list))\n"
"    local name, frame = load_frame_from_rule(rule_action_key, count, sid, max_score)\n"
"    while not frame do\n"
"        rule_action_key = redis.call(\"lpop\", action_list)\n"
"        if not rule_action_key then\n"
"            return nil, nil\n"
"        end\n"
"        count = tonumber(redis.call(\"lpop\", action_list))\n"
"        name, frame = load_frame_from_rule(rule_action_key, count, sid, max_score)\n"
"    end\n"
"    redis.call(\"lpush\", action_list, count)\n"
"    redis.call(\"lpush\", action_list, rule_action_key)\n"
"    return name, frame\n"
"end\n"
"local load_frame = function(max_score)\n"
"    local current_action = redis.call(\"zrange\", action_key, 0, 0, \"withscores\")\n"
"    if #current_action == 0 or (tonumber(current_action[2]) > (max_score + 5)) then\n"
"        return nil, nil, nil\n"
"    end\n"
"    local sid = current_action[1]\n"
"    local name, frame = load_frame_from_sid(sid, max_score)\n"
"    while not frame do\n"
"        redis.call(\"zremrangebyrank\", action_key, 0, 0)\n"
"        current_action = redis.call(\"zrange\", action_key, 0, 0, \"withscores\")\n"
"        if #current_action == 0 or (tonumber(current_action[2]) > (max_score + 5)) then\n"
"            return nil, nil, nil\n"
"        end\n"
"        sid = current_action[1]\n"
"        name, frame = load_frame_from_sid(sid, max_score)\n"
"    end\n"
"    return sid, name, frame\n"
"end\n"
"%slocal sid, action_name, frame = load_frame(tonumber(ARGV[2]))\n"
"if frame then\n"
"    redis.call(\"zincrby\", action_key, tonumber(ARGV[1]), sid)\n"
"    local state_fact = redis.call(\"hget\", state_key, sid .. \"!f\")\n"
"    local state = redis.call(\"hget\", state_key, sid)\n"
"    return {sid, state_fact, state, cjson.encode({[action_name] = frame})}\n"
"end\n",
                name,
                name,
                name,
                name,
                name,
                peekActionLua)  == -1) {
        return ERR_OUT_OF_MEMORY;
    }
    free(peekActionLua);
    redisAppendCommand(reContext, "SCRIPT LOAD %s", lua);
    redisGetReply(reContext, (void**)&reply);
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("%s\n", reply->str);
        freeReplyObject(reply);
        free(lua);
        return ERR_REDIS_ERROR;
    }

    strncpy(rulesBinding->peekActionHash, reply->str, 40);
    rulesBinding->peekActionHash[40] = '\0';
    freeReplyObject(reply);
    free(lua);

    if (asprintf(&lua, 
"local key = ARGV[1]\n"
"local sid = ARGV[2]\n"
"local assert_fact = tonumber(ARGV[3])\n"
"local events_hashset = \"%s!e!\" .. sid\n"
"local facts_hashset = \"%s!f!\" .. sid\n"
"local visited_hashset = \"%s!v!\" .. sid\n"
"local message = {}\n"
"local primary_message_keys = {}\n"
"local save_message = function(current_key, message, events_key, messages_key)\n"
"    redis.call(\"hsetnx\", messages_key, message[\"id\"], cmsgpack.pack(message))\n"
"    local primary_key = primary_message_keys[current_key](message)\n"
"    local packed_message_list\n"
"    if primary_key then\n"
"        packed_message_list = redis.call(\"hget\", events_key, primary_key)\n"
"    else\n"
"        packed_message_list = redis.call(\"get\", events_key)\n"
"    end\n"
"    local message_list = {}\n"
"    if packed_message_list then\n"
"        message_list = cmsgpack.unpack(packed_message_list)\n"
"    end\n"
"    table.insert(message_list, message[\"id\"])\n"
"    if primary_key then\n"
"        redis.call(\"hset\", events_key, primary_key, cmsgpack.pack(message_list))\n"
"    else\n"
"        redis.call(\"set\", events_key, cmsgpack.pack(message_list))\n"
"    end\n"
"end\n"
"for index = 4, #ARGV, 3 do\n"
"    if ARGV[index + 2] == \"1\" then\n"
"        message[ARGV[index]] = ARGV[index + 1]\n"
"    elseif ARGV[index + 2] == \"2\" or  ARGV[index + 2] == \"3\" then\n"
"        message[ARGV[index]] = tonumber(ARGV[index + 1])\n"
"    elseif ARGV[index + 2] == \"4\" then\n"
"        if ARGV[index + 1] == \"true\" then\n"
"            message[ARGV[index]] = true\n"
"        else\n"
"            message[ARGV[index]] = false\n"
"        end\n"
"    end\n"
"end\n"
"local mid = message[\"id\"]\n"
"if redis.call(\"hsetnx\", visited_hashset, message[\"id\"], 1) == 0 then\n"
"    if assert_fact == 0 then\n"
"        if not redis.call(\"hget\", events_hashset, mid) then\n"
"            return false\n"
"        end\n"
"    else\n"
"        if not redis.call(\"hget\", facts_hashset, mid) then\n"
"            return false\n"
"        end\n"
"    end\n"
"end\n"
"%sif assert_fact == 1 then\n"
"    message[\"$f\"] = 1\n"
"    save_message(key, message, key .. \"!f!\" .. sid, facts_hashset)\n"
"else\n"
"    save_message(key, message, key .. \"!e!\" .. sid, events_hashset)\n"
"end\n",
                name,
                name,
                name, 
                addMessageLua)  == -1) {
        return ERR_OUT_OF_MEMORY;
    }

    free(addMessageLua);
    redisAppendCommand(reContext, "SCRIPT LOAD %s", lua);
    redisGetReply(reContext, (void**)&reply);
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("%s\n", reply->str);
        freeReplyObject(reply);
        free(lua);
        return ERR_REDIS_ERROR;
    }

    strncpy(rulesBinding->addMessageHash, reply->str, 40);
    rulesBinding->addMessageHash[40] = '\0';
    freeReplyObject(reply);
    free(lua);

    if (asprintf(&lua, 
"local delete_frame = function(key)\n"
"    local rule_action_key = redis.call(\"lpop\", key)\n"
"    local count = tonumber(redis.call(\"lpop\", key))\n"
"    if count == 0 then\n"
"        local packed_frame = redis.call(\"lpop\", rule_action_key)\n"
"        while packed_frame ~= \"0\" do\n"
"            packed_frame = redis.call(\"lpop\", rule_action_key)\n"
"        end\n"
"    else\n"
"        for i = 0, count - 1, 1 do\n"
"            redis.call(\"lpop\", rule_action_key)\n"
"        end\n"
"    end\n"
"    return (redis.call(\"llen\", key) > 0)\n"
"end\n"
"local sid = ARGV[1]\n"
"local max_score = tonumber(ARGV[2])\n"
"local action_key = \"%s!a\"\n"
"if delete_frame(action_key .. \"!\" .. sid) then\n"
"    redis.call(\"zadd\", action_key, max_score, sid)\n"
"else\n"
"    redis.call(\"zrem\", action_key, sid)\n"
"end\n", name)  == -1) {
        return ERR_OUT_OF_MEMORY;
    }

    redisAppendCommand(reContext, "SCRIPT LOAD %s", lua);
    redisGetReply(reContext, (void**)&reply);
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("%s\n", reply->str);
        freeReplyObject(reply);
        free(lua);
        return ERR_REDIS_ERROR;
    }

    strncpy(rulesBinding->removeActionHash, reply->str, 40);
    rulesBinding->removeActionHash[40] = '\0';
    freeReplyObject(reply);
    free(lua);

    if (asprintf(&lua, 
"local partition_key = \"%s!p\"\n"
"local res = redis.call(\"hget\", partition_key, ARGV[1])\n"
"if (not res) then\n"
"   res = redis.call(\"hincrby\", partition_key, \"index\", 1)\n"
"   res = res %% tonumber(ARGV[2])\n"
"   redis.call(\"hset\", partition_key, ARGV[1], res)\n"
"end\n"
"return tonumber(res)\n", name)  == -1) {
        return ERR_OUT_OF_MEMORY;
    }

    redisAppendCommand(reContext, "SCRIPT LOAD %s", lua); 
    redisGetReply(reContext, (void**)&reply);
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("%s\n", reply->str);
        freeReplyObject(reply);
        return ERR_REDIS_ERROR;
    }

    strncpy(rulesBinding->partitionHash, reply->str, 40);
    rulesBinding->partitionHash[40] = '\0';
    freeReplyObject(reply);
    free(lua);

    if (asprintf(&lua,
"local timer_key = \"%s!t\"\n"
"local timestamp = tonumber(ARGV[1])\n"
"local res = redis.call(\"zrangebyscore\", timer_key, 0, timestamp, \"limit\", 0, 10)\n"
"if #res > 0 then\n"
"  for i = 0, #res, 1 do\n"
"    redis.call(\"zincrby\", timer_key, 10, res[i])\n"
"  end\n"
"  return res\n"
"end\n"
"return 0\n", name)  == -1) {
        return ERR_OUT_OF_MEMORY;
    }

    redisAppendCommand(reContext, "SCRIPT LOAD %s", lua);
    redisGetReply(reContext, (void**)&reply);
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("%s\n", reply->str);
        freeReplyObject(reply);
        return ERR_REDIS_ERROR;
    }

    strncpy(rulesBinding->timersHash, reply->str, 40);
    rulesBinding->timersHash[40] = '\0';
    freeReplyObject(reply);
    free(lua);

    char *sessionHashset = malloc((nameLength + 3) * sizeof(char));
    if (!sessionHashset) {
        return ERR_OUT_OF_MEMORY;
    }

    strncpy(sessionHashset, name, nameLength);
    sessionHashset[nameLength] = '!';
    sessionHashset[nameLength + 1] = 's';
    sessionHashset[nameLength + 2] = '\0';
    rulesBinding->sessionHashset = sessionHashset;

    char *factsHashset = malloc((nameLength + 3) * sizeof(char));
    if (!factsHashset) {
        return ERR_OUT_OF_MEMORY;
    }

    strncpy(factsHashset, name, nameLength);
    factsHashset[nameLength] = '!';
    factsHashset[nameLength + 1] = 'f';
    factsHashset[nameLength + 2] = '\0';
    rulesBinding->factsHashset = factsHashset;

    char *eventsHashset = malloc((nameLength + 3) * sizeof(char));
    if (!eventsHashset) {
        return ERR_OUT_OF_MEMORY;
    }

    strncpy(eventsHashset, name, nameLength);
    eventsHashset[nameLength] = '!';
    eventsHashset[nameLength + 1] = 'e';
    eventsHashset[nameLength + 2] = '\0';
    rulesBinding->eventsHashset = eventsHashset;

    char *timersSortedset = malloc((nameLength + 3) * sizeof(char));
    if (!timersSortedset) {
        return ERR_OUT_OF_MEMORY;
    }

    strncpy(timersSortedset, name, nameLength);
    timersSortedset[nameLength] = '!';
    timersSortedset[nameLength + 1] = 't';
    timersSortedset[nameLength + 2] = '\0';
    rulesBinding->timersSortedset = timersSortedset;

    return RULES_OK;
}

unsigned int bindRuleset(void *handle, 
                         char *host, 
                         unsigned int port, 
                         char *password) {
    ruleset *tree = (ruleset*)handle;
    bindingsList *list;
    if (tree->bindingsList) {
        list = tree->bindingsList;
    }
    else {
        list = malloc(sizeof(bindingsList));
        if (!list) {
            return ERR_OUT_OF_MEMORY;
        }

        list->bindings = NULL;
        list->bindingsLength = 0;
        list->lastBinding = 0;
        list->lastTimersBinding = 0;
        tree->bindingsList = list;
    }

    redisContext *reContext;
    if (port == 0) {
        reContext = redisConnectUnix(host);
    } else {
        reContext = redisConnect(host, port);
    }
    
    if (reContext->err) {
        redisFree(reContext);
        return ERR_CONNECT_REDIS;
    }

    if (password != NULL) {
        int result = redisAppendCommand(reContext, "auth %s", password);
        if (result != REDIS_OK) {
            return ERR_REDIS_ERROR;
        }

        redisReply *reply;
        result = redisGetReply(reContext, (void**)&reply);
        if (result != REDIS_OK) {
            return ERR_REDIS_ERROR;
        }
        
        if (reply->type == REDIS_REPLY_ERROR) {
            freeReplyObject(reply);   
            return ERR_REDIS_ERROR;
        }

        freeReplyObject(reply);
    }

    if (!list->bindings) {
        list->bindings = malloc(sizeof(binding));
    }
    else {
        list->bindings = realloc(list->bindings, sizeof(binding) * (list->bindingsLength + 1));
    }

    if (!list->bindings) {
        redisFree(reContext);
        return ERR_OUT_OF_MEMORY;
    }
    list->bindings[list->bindingsLength].reContext = reContext;
    ++list->bindingsLength;
    return loadCommands(tree, &list->bindings[list->bindingsLength -1]);
}

unsigned int deleteBindingsList(ruleset *tree) {
    bindingsList *list = tree->bindingsList;
    if (tree->bindingsList != NULL) {
        for (unsigned int i = 0; i < list->bindingsLength; ++i) {
            binding *currentBinding = &list->bindings[i];
            redisFree(currentBinding->reContext);
            free(currentBinding->timersSortedset);
            free(currentBinding->sessionHashset);
            free(currentBinding->factsHashset);
            free(currentBinding->eventsHashset);
            free(currentBinding->hashArray);
        }

        free(list->bindings);
        free(list);
    }
    return RULES_OK;
}

unsigned int getBindingIndex(ruleset *tree, unsigned int sidHash, unsigned int *bindingIndex) {
    bindingsList *list = tree->bindingsList;
    binding *firstBinding = &list->bindings[0];
    redisContext *reContext = firstBinding->reContext;

    int result = redisAppendCommand(reContext, 
                                    "evalsha %s 0 %d %d", 
                                    firstBinding->partitionHash, 
                                    sidHash, 
                                    list->bindingsLength);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }

    redisReply *reply;
    result = redisGetReply(reContext, (void**)&reply);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }
    
    if (reply->type == REDIS_REPLY_ERROR) {
        freeReplyObject(reply);
        return ERR_REDIS_ERROR;
    } 

    *bindingIndex = reply->integer;
    freeReplyObject(reply);
    return RULES_OK;
}

unsigned int formatEvalMessage(void *rulesBinding, 
                               char *key, 
                               char *sid, 
                               char *mid,
                               char *message, 
                               jsonProperty *allProperties,
                               unsigned int propertiesLength,
                               unsigned int actionIndex,
                               unsigned char assertFact,
                               char **command) {
    binding *bindingContext = (binding*)rulesBinding;
    functionHash *currentAssertHash = &bindingContext->hashArray[actionIndex];
    time_t currentTime = time(NULL);
    char score[11];
#ifdef _WIN32
	sprintf_s(score, 11, "%ld", currentTime);
	char **argv = (char **)_alloca(sizeof(char*)*(8 + propertiesLength * 3));
	size_t *argvl = (size_t *)_alloca(sizeof(size_t)*(8 + propertiesLength * 3));
#else
	snprintf(score, 11, "%ld", currentTime);
	char *argv[8 + propertiesLength * 3];
	size_t argvl[8 + propertiesLength * 3];
#endif

    argv[0] = "evalsha";
    argvl[0] = 7;
    argv[1] = *currentAssertHash;
    argvl[1] = 40;
    argv[2] = "0";
    argvl[2] = 1;
    argv[3] = key;
    argvl[3] = strlen(key);
    argv[4] = sid;
    argvl[4] = strlen(sid);
    argv[5] = mid;
    argvl[5] = strlen(mid);
    argv[6] = score;
    argvl[6] = 10;
    argv[7] = assertFact ? "1" : "0";
    argvl[7] = 1;

    for (unsigned int i = 0; i < propertiesLength; ++i) {
        argv[8 + i * 3] = message + allProperties[i].nameOffset;
        argvl[8 + i * 3] = allProperties[i].nameLength;
        argv[8 + i * 3 + 1] = message + allProperties[i].valueOffset;
        if (allProperties[i].type == JSON_STRING) {
            argvl[8 + i * 3 + 1] = allProperties[i].valueLength;
        } else {
            argvl[8 + i * 3 + 1] = allProperties[i].valueLength + 1;
        }

        switch(allProperties[i].type) {
            case JSON_STRING:
                argv[8 + i * 3 + 2] = "1";
                break;
            case JSON_INT:
                argv[8 + i * 3 + 2] = "2";
                break;
            case JSON_DOUBLE:
                argv[8 + i * 3 + 2] = "3";
                break;
            case JSON_BOOL:
                argv[8 + i * 3 + 2] = "4";
                break;
            case JSON_ARRAY:
                argv[8 + i * 3 + 2] = "5";
                break;
            case JSON_NIL:
                argv[8 + i * 3 + 2] = "7";
                break;
        }
        argvl[8 + i * 3 + 2] = 1; 
    }

    int result = redisFormatCommandArgv(command, 8 + propertiesLength * 3, (const char**)argv, argvl); 
    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int formatStoreMessage(void *rulesBinding, 
                                char *key, 
                                char *sid, 
                                char *message, 
                                jsonProperty *allProperties,
                                unsigned int propertiesLength,
                                unsigned char storeFact,
                                char **command) {
    binding *bindingContext = (binding*)rulesBinding;
#ifdef _WIN32
	char **argv = (char **)_alloca(sizeof(char*)*(6 + propertiesLength * 3));
	size_t *argvl = (size_t *)_alloca(sizeof(size_t)*(6 + propertiesLength * 3));
#else
	char *argv[6 + propertiesLength * 3];
	size_t argvl[6 + propertiesLength * 3];
#endif

    argv[0] = "evalsha";
    argvl[0] = 7;
    argv[1] = bindingContext->addMessageHash;
    argvl[1] = 40;
    argv[2] = "0";
    argvl[2] = 1;
    argv[3] = key;
    argvl[3] = strlen(key);
    argv[4] = sid;
    argvl[4] = strlen(sid);
    argv[5] = storeFact ? "1" : "0";
    argvl[5] = 1;

    for (unsigned int i = 0; i < propertiesLength; ++i) {
        argv[6 + i * 3] = message + allProperties[i].nameOffset;
        argvl[6 + i * 3] = allProperties[i].nameLength;
        argv[6 + i * 3 + 1] = message + allProperties[i].valueOffset;
        if (allProperties[i].type == JSON_STRING) {
            argvl[6 + i * 3 + 1] = allProperties[i].valueLength;
        } else {
            argvl[6 + i * 3 + 1] = allProperties[i].valueLength + 1;
        }

        switch(allProperties[i].type) {
            case JSON_STRING:
                argv[6 + i * 3 + 2] = "1";
                break;
            case JSON_INT:
                argv[6 + i * 3 + 2] = "2";
                break;
            case JSON_DOUBLE:
                argv[6 + i * 3 + 2] = "3";
                break;
            case JSON_BOOL:
                argv[6 + i * 3 + 2] = "4";
                break;
        }
        argvl[6 + i * 3 + 2] = 1; 
    }

    int result = redisFormatCommandArgv(command, 6 + propertiesLength * 3, (const char**)argv, argvl); 
    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int formatStoreSession(void *rulesBinding, 
                                char *sid, 
                                char *state,
                                unsigned char tryExists, 
                                char **command) {
    binding *currentBinding = (binding*)rulesBinding;

    int result;
    if (tryExists) {
        result = redisFormatCommand(command,
                                    "hsetnx %s %s %s", 
                                    currentBinding->sessionHashset, 
                                    sid, 
                                    state);
    } else {
        result = redisFormatCommand(command,
                                    "hset %s %s %s", 
                                    currentBinding->sessionHashset, 
                                    sid, 
                                    state);
    }

    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int formatStoreSessionFact(void *rulesBinding, 
                                    char *sid, 
                                    char *message,
                                    unsigned char tryExists, 
                                    char **command) {
    binding *currentBinding = (binding*)rulesBinding;

    int result;
    if (tryExists) {
        result = redisFormatCommand(command,
                                    "hsetnx %s %s!f %s", 
                                    currentBinding->sessionHashset, 
                                    sid, 
                                    message);
    } else {
        result = redisFormatCommand(command,
                                    "hset %s %s!f %s", 
                                    currentBinding->sessionHashset, 
                                    sid, 
                                    message);
    }

    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int formatRemoveTimer(void *rulesBinding, 
                               char *timer, 
                               char **command) {
    binding *currentBinding = (binding*)rulesBinding;
    int result = redisFormatCommand(command,
                                    "zrem %s %s", 
                                    currentBinding->timersSortedset, 
                                    timer);
    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int formatRemoveAction(void *rulesBinding, 
                                char *sid, 
                                char **command) {
    binding *bindingContext = (binding*)rulesBinding;
    time_t currentTime = time(NULL);

    int result = redisFormatCommand(command,
                                    "evalsha %s 0 %s %ld", 
                                    bindingContext->removeActionHash, 
                                    sid, 
                                    currentTime); 
    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int formatRemoveMessage(void *rulesBinding, 
                                  char *sid, 
                                  char *mid,
                                  unsigned char removeFact,
                                  char **command) {
    binding *currentBinding = (binding*)rulesBinding;

    int result = 0;
    if (removeFact) {
        result = redisFormatCommand(command,
                                    "hdel %s!%s %s", 
                                    currentBinding->factsHashset, 
                                    sid, 
                                    mid);
    } else {
        result = redisFormatCommand(command,
                                    "hdel %s!%s %s", 
                                    currentBinding->eventsHashset, 
                                    sid, 
                                    mid);
    }

    if (result == 0) {
        return ERR_OUT_OF_MEMORY;
    }
    return RULES_OK;
}

unsigned int startNonBlockingBatch(void *rulesBinding,
                                   char **commands,
                                   unsigned short commandCount,
                                   unsigned short *replyCount) {
    *replyCount = commandCount;
    if (commandCount == 0) {
        return RULES_OK;
    }

    unsigned int result = RULES_OK;
    binding *currentBinding = (binding*)rulesBinding;
    redisContext *reContext = currentBinding->reContext;
    if (commandCount > 1) {
        ++(*replyCount);
        result = redisAppendCommand(reContext, "multi");
        if (result != REDIS_OK) {
            for (unsigned short i = 0; i < commandCount; ++i) {
                free(commands[i]);
            }

            return ERR_REDIS_ERROR;
        }
    }

    for (unsigned short i = 0; i < commandCount; ++i) {
        sds newbuf;
        newbuf = sdscatlen(reContext->obuf, commands[i], strlen(commands[i]));
        if (newbuf == NULL) {
            return ERR_OUT_OF_MEMORY;
        }

        reContext->obuf = newbuf;
        free(commands[i]);
    }

    if (commandCount > 1) {
        ++(*replyCount);
        unsigned int result = redisAppendCommand(reContext, "exec");
        if (result != REDIS_OK) {
            return ERR_REDIS_ERROR;
        }
    }

    int wdone = 0;
    do {
        if (redisBufferWrite(reContext, &wdone) == REDIS_ERR) {
            return ERR_REDIS_ERROR;
        }
    } while (!wdone);

    return result;
}

unsigned int completeNonBlockingBatch(void *rulesBinding,
                                      unsigned short replyCount) {
    if (replyCount == 0) {
        return RULES_OK;
    }

    unsigned int result = RULES_OK;
    binding *currentBinding = (binding*)rulesBinding;
    redisContext *reContext = currentBinding->reContext;
    redisReply *reply;
    for (unsigned short i = 0; i < replyCount; ++i) {
        result = redisGetReply(reContext, (void**)&reply);
        if (result != REDIS_OK) {
            result = ERR_REDIS_ERROR;
        } else {
            if (reply->type == REDIS_REPLY_ERROR) {
                printf("error %d %s\n", i, reply->str);
                result = ERR_REDIS_ERROR;
            }

            freeReplyObject(reply);    
        } 
    }
    
    return result;
}

unsigned int executeBatch(void *rulesBinding,
                          char **commands,
                          unsigned short commandCount) {
    if (commandCount == 0) {
        return RULES_OK;
    }

    unsigned int result = RULES_OK;
    unsigned short replyCount = commandCount;
    binding *currentBinding = (binding*)rulesBinding;
    redisContext *reContext = currentBinding->reContext;
    if (commandCount > 1) {
        ++replyCount;
        result = redisAppendCommand(reContext, "multi");
        if (result != REDIS_OK) {
            for (unsigned short i = 0; i < commandCount; ++i) {
                free(commands[i]);
            }

            return ERR_REDIS_ERROR;
        }
    }

    for (unsigned short i = 0; i < commandCount; ++i) {
        sds newbuf;
        newbuf = sdscatlen(reContext->obuf, commands[i], strlen(commands[i]));
        if (newbuf == NULL) {
            return ERR_OUT_OF_MEMORY;
        }

        reContext->obuf = newbuf;
        free(commands[i]);
    }

    if (commandCount > 1) {
        ++replyCount;
        unsigned int result = redisAppendCommand(reContext, "exec");
        if (result != REDIS_OK) {
            return ERR_REDIS_ERROR;
        }
    }

    redisReply *reply;
    for (unsigned short i = 0; i < replyCount; ++i) {
        result = redisGetReply(reContext, (void**)&reply);
        if (result != REDIS_OK) {
            result = ERR_REDIS_ERROR;
        } else {
            if (reply->type == REDIS_REPLY_ERROR) {
                printf("%s\n", reply->str);
                result = ERR_REDIS_ERROR;
            }

            freeReplyObject(reply);    
        } 
    }
    
    return result;
}

unsigned int removeMessage(void *rulesBinding, char *sid, char *mid) {
    binding *currentBinding = (binding*)rulesBinding;
    redisContext *reContext = currentBinding->reContext;  
    int result = redisAppendCommand(reContext, 
                                    "hdel %s!%s %s", 
                                    currentBinding->factsHashset, 
                                    sid, 
                                    mid);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }

    redisReply *reply;
    result = redisGetReply(reContext, (void**)&reply);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        freeReplyObject(reply);
        return ERR_REDIS_ERROR;
    }
    
    freeReplyObject(reply);    
    return RULES_OK;
}

unsigned int peekAction(ruleset *tree, void **bindingContext, redisReply **reply) {
    bindingsList *list = tree->bindingsList;
    for (unsigned int i = 0; i < list->bindingsLength; ++i) {
        binding *currentBinding = &list->bindings[list->lastBinding % list->bindingsLength];
        ++list->lastBinding;
        redisContext *reContext = currentBinding->reContext;
        time_t currentTime = time(NULL);

        int result = redisAppendCommand(reContext, 
                                        "evalsha %s 0 %d %ld", 
                                        currentBinding->peekActionHash, 
                                        60,
                                        currentTime); 
        if (result != REDIS_OK) {
            continue;
        }

        result = redisGetReply(reContext, (void**)reply);
        if (result != REDIS_OK) {
            return ERR_REDIS_ERROR;
        }

        if ((*reply)->type == REDIS_REPLY_ERROR) {
            printf("%s\n", (*reply)->str);
            freeReplyObject(*reply);
            return ERR_REDIS_ERROR;
        }
        
        if ((*reply)->type == REDIS_REPLY_ARRAY) {
            *bindingContext = currentBinding;
            return RULES_OK;
        } else {
            freeReplyObject(*reply);
        }
    }

    return ERR_NO_ACTION_AVAILABLE;
}

unsigned int peekTimers(ruleset *tree, void **bindingContext, redisReply **reply) {
    bindingsList *list = tree->bindingsList;
    for (unsigned int i = 0; i < list->bindingsLength; ++i) {
        binding *currentBinding = &list->bindings[list->lastTimersBinding % list->bindingsLength];
        ++list->lastTimersBinding;
        redisContext *reContext = currentBinding->reContext;
        time_t currentTime = time(NULL);

        int result = redisAppendCommand(reContext, 
                                        "evalsha %s 0 %ld", 
                                        currentBinding->timersHash,
                                        currentTime); 
        if (result != REDIS_OK) {
            continue;
        }

        result = redisGetReply(reContext, (void**)reply);
        if (result != REDIS_OK) {
            return ERR_REDIS_ERROR;
        }

        if ((*reply)->type == REDIS_REPLY_ERROR) {
            freeReplyObject(*reply);
            return ERR_REDIS_ERROR;
        }
        
        if ((*reply)->type == REDIS_REPLY_ARRAY) {
            *bindingContext = currentBinding;
            return RULES_OK;
        } else {
            freeReplyObject(*reply);
        }
    }

    return ERR_NO_TIMERS_AVAILABLE;
}

unsigned int registerTimer(void *rulesBinding, unsigned int duration, char *timer) {
    binding *currentBinding = (binding*)rulesBinding;
    redisContext *reContext = currentBinding->reContext;   
    time_t currentTime = time(NULL);

    int result = redisAppendCommand(reContext, 
                                    "zadd %s %ld %s", 
                                    currentBinding->timersSortedset, 
                                    currentTime + duration, 
                                    timer);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }
    
    redisReply *reply;
    result = redisGetReply(reContext, (void**)&reply);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        freeReplyObject(reply);
        return ERR_REDIS_ERROR;
    }

    freeReplyObject(reply);    
    return RULES_OK;
}

unsigned int getSession(void *rulesBinding, char *sid, char **state) {
    binding *currentBinding = (binding*)rulesBinding;
    redisContext *reContext = currentBinding->reContext; 
    unsigned int result = redisAppendCommand(reContext, 
                                "hget %s %s", 
                                currentBinding->sessionHashset, 
                                sid);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }

    redisReply *reply;
    result = redisGetReply(reContext, (void**)&reply);
    if (result != REDIS_OK) {
        return ERR_REDIS_ERROR;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        freeReplyObject(reply);
        return ERR_REDIS_ERROR;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        freeReplyObject(reply);
        return ERR_NEW_SESSION;
    }

    *state = malloc(strlen(reply->str) * sizeof(char));
    if (!*state) {
        return ERR_OUT_OF_MEMORY;
    }
    strcpy(*state, reply->str);
    freeReplyObject(reply); 
    return REDIS_OK;
}


