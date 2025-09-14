from dataclasses import dataclass
from typing import List
from .enums import PieceType, Color

@dataclass
class Piece:
    type: PieceType
    color: Color
    abilities: List[PieceType]  # List of movement abilities this piece has
    position: tuple  # (row, col)
    has_moved: bool = False
    
    def __post_init__(self):
        if not self.abilities:
            self.abilities = [self.type]  # Start with own movement type

    def add_ability(self, ability: PieceType):
        if ability not in self.abilities:
            self.abilities.append(ability)

    def remove_ability(self, ability: PieceType):
        if ability in self.abilities:
            self.abilities.remove(ability)

    def has_ability(self, ability: PieceType) -> bool:
        return ability in self.abilities

    def to_dict(self):
        return {
            'type': self.type.value,
            'color': self.color.value,
            'abilities': [a.value for a in self.abilities],
            'position': self.position,
            'has_moved': self.has_moved
        }

    @staticmethod
    def from_dict(data):
        return Piece(
            type=PieceType(data['type']),
            color=Color(data['color']),
            abilities=[PieceType(a) for a in data.get('abilities', [data['type']])],
            position=tuple(data['position']),
            has_moved=bool(data.get('has_moved', False))
        )
        
