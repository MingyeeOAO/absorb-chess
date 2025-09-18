from PIL import Image
import os
import sys

def has_content(img_piece, threshold=0.1):
    """
    Check if an image piece has enough non-transparent content.
    Returns True if the piece has more non-transparent pixels than the threshold.
    """
    if img_piece.mode == 'RGBA':
        # Count non-transparent pixels
        non_transparent = sum(1 for x in range(img_piece.width)
                            for y in range(img_piece.height)
                            if img_piece.getpixel((x, y))[3] > 0)
        total_pixels = img_piece.width * img_piece.height
        return (non_transparent / total_pixels) > threshold
    else:
        # For non-RGBA images, check if the image is not completely white/black
        extremes = sum(1 for x in range(img_piece.width)
                      for y in range(img_piece.height)
                      if img_piece.getpixel((x, y)) in [0, 255])
        total_pixels = img_piece.width * img_piece.height
        return (extremes / total_pixels) < (1 - threshold)

def split_image(image_path, piece_size, output_dir='pieces', prefix='piece'):
    """
    Split an image into pieces of specified size.
    
    Args:
        image_path: Path to the source image
        piece_size: Size of each piece (width and height in pixels)
        output_dir: Directory to save the output pieces
        prefix: Prefix for the output filenames
    """
    try:
        # Create output directory if it doesn't exist
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)

        # Open and load the image
        with Image.open(image_path) as img:
            # Convert to RGBA if not already
            if img.mode != 'RGBA':
                img = img.convert('RGBA')

            # Calculate number of pieces that fit in each dimension
            n_cols = img.width // piece_size
            n_rows = img.height // piece_size

            print(f"Image dimensions: {img.width}x{img.height}")
            print(f"Piece size: {piece_size}x{piece_size}")
            print(f"Will create up to {n_rows}x{n_cols} pieces")

            # Counter for pieces with content
            piece_count = 0

            # Split the image
            for i in range(n_rows):
                for j in range(n_cols):
                    # Calculate piece coordinates
                    left = j * piece_size
                    top = i * piece_size
                    right = left + piece_size
                    bottom = top + piece_size
                    
                    # Skip if we go beyond image boundaries
                    if right > img.width or bottom > img.height:
                        continue

                    # Extract piece
                    piece = img.crop((left, top, right, bottom))

                    # Check if piece has content
                    if has_content(piece):
                        # Save piece
                        output_path = os.path.join(output_dir, f'{prefix}_{piece_count}.png')
                        piece.save(output_path, 'PNG')
                        print(f'Saved piece {piece_count} to {output_path}')
                        piece_count += 1

            print(f'Successfully split image into {piece_count} pieces with content')
            return piece_count

    except Exception as e:
        print(f'Error processing image: {e}')
        return 0

def main():
    if len(sys.argv) < 3:
        print('Usage: python split_sprites.py <image_path> <piece_size> [output_dir] [prefix]')
        print('Example: python split_sprites.py spritesheet.png 333 pieces piece')
        print('This will split the image into 333x333 pixel pieces')
        return

    image_path = sys.argv[1]
    piece_size = int(sys.argv[2])
    output_dir = sys.argv[3] if len(sys.argv) > 3 else 'pieces'
    prefix = sys.argv[4] if len(sys.argv) > 4 else 'piece'

    if not os.path.exists(image_path):
        print(f'Error: Image file {image_path} not found')
        return

    split_image(image_path, piece_size, output_dir, prefix)

if __name__ == '__main__':
    main()
