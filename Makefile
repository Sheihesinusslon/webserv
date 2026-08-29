NAME            = webserv
NAME_BONUS      = webserv_bonus

CXX             = c++
CXXFLAGS        = -Wall -Wextra -Werror -std=c++98

MAKEFLAGS       += --no-print-directory

SRC_DIR         = src
OBJ_DIR         = obj
OBJ_DIR_BONUS   = obj_bonus
INC_DIR         = include
INC_DIR_BONUS   = include/bonus

SRC_CONFIG = \
	config/Config.cpp \
	config/ConfigParser.cpp \
	config/ConfigTokenizer.cpp \
	config/Listener.cpp \
	config/LocationConfig.cpp \
	config/ServerConfig.cpp

SRC = \
	main.cpp \
	$(SRC_CONFIG)

SRC_BONUS = \
	bonus/bonus.cpp

SRCS        := $(addprefix $(SRC_DIR)/,$(SRC))
SRCS_BONUS  := $(addprefix $(SRC_DIR)/,$(SRC_BONUS))

OBJS            = $(SRCS:%.cpp=$(OBJ_DIR)/%.o)
OBJS_BONUS_MAIN = $(SRCS:%.cpp=$(OBJ_DIR_BONUS)/%.o)
OBJS_BONUS      = $(SRCS_BONUS:%.cpp=$(OBJ_DIR_BONUS)/%.o)

DEPS        = $(OBJS:.o=.d)
DEPS_BONUS  = $(OBJS_BONUS_MAIN:.o=.d) $(OBJS_BONUS:.o=.d)

INCLUDES        = -I$(INC_DIR)
INCLUDES_BONUS  = -I$(INC_DIR) -I$(INC_DIR_BONUS)

CXXFLAGS_BONUS  = $(CXXFLAGS) -DBONUS

all: $(NAME)

bonus: $(NAME_BONUS)

$(NAME): $(OBJS) Makefile
	@echo "Linking $(NAME)..."
	@$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@echo "✓ $(NAME) compiled successfully"

$(NAME_BONUS): $(OBJS_BONUS_MAIN) $(OBJS_BONUS) Makefile
	@echo "Linking $(NAME_BONUS)..."
	@$(CXX) $(CXXFLAGS_BONUS) $(OBJS_BONUS_MAIN) $(OBJS_BONUS) -o $(NAME_BONUS)
	@echo "✓ $(NAME_BONUS) compiled successfully"

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJ_DIR_BONUS)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling bonus $<..."
	@$(CXX) $(CXXFLAGS_BONUS) $(INCLUDES_BONUS) -MMD -MP -c $< -o $@

clean:
	@rm -rf $(OBJ_DIR) $(OBJ_DIR_BONUS)
	@echo "✓ Object files cleaned"

fclean: clean
	@rm -f $(NAME) $(NAME_BONUS)
	@echo "✓ $(NAME) removed"

re: fclean all

test: all
	@bash tests/run_tests.sh

-include $(DEPS)
-include $(DEPS_BONUS)

.PHONY: all bonus clean fclean re test
