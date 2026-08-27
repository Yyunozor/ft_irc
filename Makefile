NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98
DEPFLAGS	= -MMD -MP
LDFLAGS		=
INCDIR		= inc
SRCDIR		= src
OBJDIR		= obj

# ---------------------------------------------------------------------------
# Debug build: `make DEBUG=1`
# Never on by default — the submitted build must use the required flags only.
# AddressSanitizer catches the use-after-free we risk when a Client is deleted
# while a Channel still holds a pointer to it.
# ---------------------------------------------------------------------------
ifeq ($(DEBUG), 1)
	CXXFLAGS	+= -g3 -fsanitize=address,undefined
	LDFLAGS		+= -fsanitize=address,undefined
endif

SRCS		= $(notdir $(wildcard $(SRCDIR)/*.cpp))
OBJS		= $(addprefix $(OBJDIR)/, $(SRCS:.cpp=.o))
DEPS		= $(OBJS:.o=.d)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $(NAME)

# -MMD -MP makes the compiler emit obj/<name>.d listing every header this
# translation unit actually includes. Re-injected below, so editing a header
# recompiles exactly the objects that depend on it -- and nothing else.
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(INCDIR) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
